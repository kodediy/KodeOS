#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A Kode Dot that is not there: the station program against a fake board.

    python3 tools/factory/ui/test_dot_sim.py [--case pass] [--events] [--out DIR]
                                             [--speed 1] [--operator-sim] [--no-open]

The fake answers the console verbs the station speaks (section 7 of the E5
spec: `test component=all`, `test_dot_*`, and the ordinary verbs the station
also uses) with the records a real board gave on the bench - board 6c:0a on
2026-11-03, recorded with the verbs of the time and moved here to the
`test_dot_*` verbs and their English records. It runs the real
`test_dot_final.py` on top of it, so the page and the reports see exactly what
a station run produces, and no Kode, no serial port and no camera are needed.

Cases (`--case`, or the environment variable TEST_DOT_CASE that
`test_dot_ui.py --simulator` passes on):

  pass        everything passes, the operator gets every step right first time
  fail        a dead pixel, NFC with no tag, the station's relays silent, and an
              operator who presses a wrong button once, lets one step time out
              and misses one touch
  nfc         everything passes but NFC, which finds no tag
  buttons     the bottom button is held from the start and the pad presses two
              directions at once on the right one
  noverdict   everything passes but the pixel check, which cannot judge
  timeouts    the operator does nothing: every button and touch step times out
  misplaced   the placement never completes (the Kode sits badly)
  no_camera   the camera on J3 never answers
  no_usb      the Kode never appears on USB

`--operator-sim` makes the fake wait for the `btn`/`touch` injection instead of
replaying the operator, which is what the station's bench mode sends.

The same fake drives tools/tests/test_factory.py, on a virtual clock there.
"""

import argparse
import base64
import bisect
import io
import itertools
import os
import random
import sys
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
FACTORY = HERE.parent
TOOLS = FACTORY.parent
for p in (str(TOOLS), str(FACTORY)):
    if p not in sys.path:
        sys.path.insert(0, p)

from kpayload import fields, quote  # noqa: E402

PORT = "/dev/fake-kode-dot"
BOOT_S = 2.4               # the console answers this long after the port opens (measured)
B64_LINE = 160             # characters of base64 in one `>|d=` record
POLL_MAX = 64              # records one `test_dot_poll` hands over at most
OPERATOR_LIMIT_S = 20.0    # the Kode's time for one button or touch step
PLACE_LOOK_S = 2.0         # seconds between two placement looks on the recorded board
PLACE_MISPLACED_LOOKS = 7  # looks the misplaced case takes before the time is up
TOUCH_PX = 64              # how far from the target a touch may land, per axis
SERIAL = "KODE1TEST00001D"
MAC_P4 = "38:44:be:07:e6:54"
WINDOW = "704,628"

INFO = [
    f'serial={SERIAL} product="KODE-DOT" board="DVT"',
    f"mac_p4={MAC_P4}",
    'slot=factory addr=0x020000 size_kb=4096 state=inactive project="kodeos" version="00.00.25" idf="v6.1"',
    'slot=ota_0 addr=0x420000 size_kb=4096 state=running project="kodeos" version="00.00.26" idf="v6.1"',
    'slot=ota_1 addr=0x820000 size_kb=4096 state=inactive project="kodeos" version="00.00.24" idf="v6.1"',
    'coredump=present owner=other task="gui" pc=0x400eca78',
    'c5=running project="kode_c5" version="2.12.9" idf="v6.0.2" recog=kode power=running wire=3 vault=0 btkeys=0 id=key',
    "log=i source=default",
    "temp=p4 c=41.3",
]

# The photos a good pass takes, in the order the Kode numbers them.
PHOTOS = ["place", "black_marks", "panel_off", "white", "grey", "red", "green", "blue",
          "led_off", "led_red", "led_green", "led_blue"]

BUTTONS = ["pad-left", "bottom", "pad-down", "top", "pad-right", "pad-up"]
TARGETS = ["80,215", "335,285"]
# The verbs that wake the station's side of the Kode (test_dot_ao.c, verb_wakes()).
WAKING_VERBS = ("test_dot_place", "test", "test_dot_relays", "test_dot_adc", "test_dot_camera")
# What the injection verb calls each button (console_ao.c, A_BTN_IDS).
BTN_IDS = {"left": "pad-left", "bot": "bottom", "down": "pad-down", "top": "top",
           "right": "pad-right", "up": "pad-up"}

# The station's rest voltages by (relay byte, ADC channel), in volts at the
# measured point - Luismi's good case (test_pads.py). The ADC answers the pin,
# which sits behind a /2 divider on every channel but A1.
GOOD_VOLTS = {(0xFF, 0): 4.80, (0xFF, 1): 1.67, (0xFF, 2): 4.89, (0xFF, 3): 4.92,
              (0xFB, 0): 4.77, (0xFB, 1): 1.68, (0xFB, 2): 4.89, (0xFB, 3): 4.92,
              (0xFE, 0): 4.65, (0xFE, 1): 1.67, (0xFE, 2): 0.01, (0xFE, 3): 4.88}


class VirtualClock:
    """Time that moves only when somebody waits: a run of minutes takes
    milliseconds, and a board that never answers is judged by the same code."""

    def __init__(self):
        self.now = 1000.0
        self.epoch = 1793260000.0 - self.now     # 2026-10-29, any fixed day

    def monotonic(self):
        return self.now

    def time(self):
        return self.epoch + self.now

    def sleep(self, seconds):
        self.now += max(0.0, seconds)

    def wait_until(self, due, timeout):
        self.now = min(max(self.now, due), self.now + timeout)


class RealClock:
    """The wall clock, for the simulator the page watches."""

    @staticmethod
    def monotonic():
        return time.monotonic()

    @staticmethod
    def time():
        return time.time()

    @staticmethod
    def sleep(seconds):
        time.sleep(seconds)

    def wait_until(self, due, timeout):
        time.sleep(max(0.0, min(due - time.monotonic(), timeout)))


def make_jpeg(label, size=(176, 128), colour=(20, 24, 30)):
    """A small JPEG with a label on it, or plain bytes without Pillow (a CRC
    does not care what the bytes are)."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return (label.encode() * 64)[:2048]
    im = Image.new("RGB", size, colour)
    d = ImageDraw.Draw(im)
    d.rectangle([4, 4, size[0] - 5, size[1] - 5], outline=(220, 220, 220), width=2)
    d.text((10, 10), label, fill=(255, 220, 0))
    out = io.BytesIO()
    im.save(out, "JPEG", quality=80)
    return out.getvalue()


def make_location():
    """What the camera sees at placement: the panel and its four marks."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return make_jpeg("location")
    im = Image.new("RGB", (352, 256), (2, 14, 4))
    d = ImageDraw.Draw(im)
    for x, y in ((60, 60), (290, 56), (62, 182), (294, 176)):
        d.rectangle([x, y, x + 16, y + 16], fill=(250, 250, 250))
    out = io.BytesIO()
    im.save(out, "JPEG", quality=80)
    return out.getvalue()


def make_colours():
    """The Kode's strip of five 96 x 64 crops: red, green, blue, white, grey."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return make_jpeg("colours")
    im = Image.new("RGB", (480, 64))
    d = ImageDraw.Draw(im)
    for k, c in enumerate(((150, 10, 10), (10, 120, 10), (10, 20, 150), (200, 200, 200), (140, 140, 140))):
        d.rectangle([k * 96, 0, k * 96 + 95, 63], fill=c)
    out = io.BytesIO()
    im.save(out, "JPEG", quality=85)
    return out.getvalue()


def blob_lines(name, data, crc=None):
    """A blob as the Kode sends it: its head, then base64 in `>|d=` records."""
    if crc is None:
        crc = zlib.crc32(data) & 0xFFFFFFFF
    text = base64.b64encode(data).decode()
    out = [f">|blob name={quote(name)} len={len(data)} crc={crc}"]
    out += [">|d=" + text[i:i + B64_LINE] for i in range(0, len(text), B64_LINE)]
    return out


class Scenario:
    """What the fake Kode does: the recorded pass, and the knobs that bend it.

    @param case            One of the cases in the module's docstring.
    @param has_test_dot    False: a kodeOS without the station verbs.
    @param operator        "recorded" replays the operator; "injected" waits
                           for `btn`/`touch` lines.
    @param photo_missing   {n: times}: photo n is left out of the first
                           `test_dot_photos` and of the next times-1 asks for it.
    @param photo_bad_crc   {n: times}: photo n arrives with a wrong CRC that
                           many times.
    @param lock_refused    True: `test_dot_lock` answers `>err` (a lock the
                           Kode could not keep).
    """

    def __init__(self, case="pass", has_test_dot=True, operator="recorded",
                 photo_missing=None, photo_bad_crc=None, serial=SERIAL, lock_refused=False):
        self.case = case
        self.has_test_dot = has_test_dot
        self.operator = operator
        self.photo_missing = dict(photo_missing or {})
        self.photo_bad_crc = dict(photo_bad_crc or {})
        self.serial = serial
        self.lock_refused = lock_refused

    # ---- placement ---------------------------------------------------------
    def place(self):
        """(seconds after the command, line) for `test_dot_place`."""
        head = [(0.9, "place mem psram_free=27853504 largest=27787264"),
                (3.3, "place init ok=1 ms=877 why=-")]
        if self.case == "no_camera":
            return [(1.0, "place state=no_camera"), (10.0, "place result=cam_missing"), (10.0, ">ok")]
        if self.case == "misplaced":
            looks = [(3.5 + k * PLACE_LOOK_S, f'place check n={k + 1} marks=2 dx=18.20 dy=-6.40 ang=12.30 '
                                              f'win="{WINDOW}"') for k in range(PLACE_MISPLACED_LOOKS)]
            return head + [(3.4, "place state=searching")] + looks + [
                (18.0, "place state=misplaced"),
                (18.0, "place result=misplaced marks=2 dx_mm=18.20 dy_mm=-6.40 ang_deg=12.30 "
                       "limit_mm=15.0 limit_deg=5.0"), (18.0, ">ok")]
        return head + [(3.4, "place state=searching"),
                       (3.5, f'place check n=1 marks=4 dx=-1.78 dy=1.09 ang=-1.00 win="{WINDOW}"'),
                       (5.4, f'place result=ok dx_mm=-1.78 dy_mm=1.09 ang_deg=-1.00 marks=4 tries=1 '
                             f'win="{WINDOW}" ms=1966'),
                       (5.4, ">ok")]

    # ---- the run -----------------------------------------------------------
    def row(self, name, ms):
        res = "pass"
        if self.case in ("fail", "nfc") and name == "nfc":
            res = "fail"
        return f"test={name} result={res} ms={ms}"

    def camera(self):
        pixels = "test=cam_pixels result=pass dead=0 stuck=0 warn=0 max_dead=2 max_stuck=1 at=\"\""
        extra = []
        if self.case == "fail":
            pixels = 'test=cam_pixels result=fail dead=1 stuck=0 warn=5 max_dead=0 max_stuck=0 at="97,275;"'
            extra = blob_lines("dead_97_275", make_jpeg("dead 97,275", (64, 64), (190, 186, 196)))
        elif self.case == "noverdict":
            pixels = "test=cam_pixels result=noverdict why=noise sat=0.000,0.000 sigma=0.040,0.066"
        return [
            "test=cam_framing result=pass marks=4 inside=1 dx_mm=-1.78 dy_mm=1.10 scale=2.234 nominal=1 "
            f'min_marks=4 rescued=0 recentred=0 win="{WINDOW}"',
            "test=cam_straight result=pass angle_deg=-1.00 persp_pct=0.2 max_angle=7 max_persp_pct=8",
        ] + [x[2:] for x in extra] + [
            pixels,
            "test=cam_lines result=pass lines=0 x=0 y0=0 y1=0 row_y=0 level=-0.042 hlevel=-0.030 "
            "max_level=-0.20 wlevel=-0.010,-0.017 whlevel=-0.044,-0.023",
            'test=cam_zones result=pass zones=0 min_ratio=1.00 limit_ratio=0.86 bright=0 at=""',
        ]

    def run_line(self):
        """The records before the run's `>ok`, which comes at the camera's end."""
        t = [(2.1, self.row("sd", 2098)), (2.8, self.row("imu", 629))]
        t += [(6.0, x[2:]) for x in blob_lines("location", make_location())]
        t += [(9.0, x) for x in self.camera()]
        t += [(11.0, "colour_info dark=0.0,1.6,0.0 white=143.0,152.1,146.5 grey=0.3598,0.3140,0.3262 "
                     "red=0.9478,0.0000,0.0522 green=0.0000,1.0000,0.0000 blue=0.0006,0.1053,0.8941"),
              (11.0, "test=cam_colour result=pass dist_grey=0.0528 dist_red=0.0240 dist_green=0.0000 "
                     "dist_blue=0.0112 wchroma=0.022 ripple=0.023 expratio=1.00 tol=0.250 retakes=0")]
        t += [(11.1, x[2:]) for x in blob_lines("colours", make_colours())]
        t += [(11.9, "test=led result=pass flux_red=25259 dist_red=0.41 flux_green=73720 dist_green=0.63 "
                     "flux_blue=104989 dist_blue=0.52 at=556,108 min_flux=4000 max_dist=0.8"),
              (12.0, "timing=camera photos_ms=4301 analysis_ms=2214 led_ms=1664 total_ms=9115")]
        return t

    def run_ring(self):
        """The records after the `>ok`, which `test_dot_poll` hands over; the
        operator's are added by the operator model."""
        return [(12.4, self.row("audio", 208)), (12.5, self.row("haptic", 600)),
                (12.6, self.row("mag", 13)), (12.7, self.row("pmic", 3)), (12.8, self.row("gauge", 2)),
                (12.9, self.row("ioexp", 0)), (13.0, self.row("clocks", 133)), (13.2, self.row("ir", 132)),
                (17.7, self.row("nfc", 4508)), (20.3, self.row("rfid", 2565)), (20.4, self.row("mem", 0))]

    def operator_script(self):
        """The recorded operator: (seconds after the camera's end, record)."""
        out, t = [], 0.3
        btn_fail, tp_fail = False, False
        for k, b in enumerate(BUTTONS, 1):
            out.append((t, f"step={k} of=6 press={b}"))
            if self.case == "timeouts" or (self.case == "fail" and k == 4):
                t += OPERATOR_LIMIT_S
                out.append((t, f"step={k} button={b} result=timeout"))
                btn_fail = True
            elif self.case == "fail" and k == 2:
                t += 1.4
                out.append((t, f"step={k} button={b} result=wrong pressed=pad-up ms=90"))
                out.append((t + 0.05, f"step={k} of=6 press={b}"))
                t += 1.6
                out.append((t, f"step={k} button={b} result=pass ms=140"))
            elif self.case == "buttons" and k == 2:
                t += 0.2
                out.append((t, f"step={k} button={b} result=held pressed=bottom ms=0"))
                btn_fail = True
            elif self.case == "buttons" and k == 5:
                t += 1.1
                out.append((t, f"step={k} button={b} result=double pressed=pad-right+pad-down ms=40"))
                out.append((t + 0.05, f"step={k} of=6 press={b}"))
                t += 1.5
                out.append((t, f"step={k} button={b} result=pass ms=150"))
            else:
                t += 1.3
                out.append((t, f"step={k} button={b} result=pass ms=131"))
            t += 0.05
        res = "pass"
        if btn_fail:
            res = "timeout" if self.case in ("timeouts", "fail") else "fail"
        out.append((t, f"test=buttons result={res} ms={int(t * 1000)}"))
        t0 = t
        for k, target in enumerate(TARGETS, 1):
            out.append((t, f"step={k} of=2 target={target}"))
            x, y = (int(v) for v in target.split(","))
            if self.case == "timeouts":
                t += OPERATOR_LIMIT_S
                out.append((t, f"step={k} target={target} result=timeout"))
                tp_fail = True
                continue
            if self.case == "fail" and k == 1:
                t += 1.8
                out.append((t, f"step={k} target={target} got={x + 90},{y + 80} result=miss"))
                out.append((t + 0.05, f"step={k} of=2 target={target}"))
            t += 2.0
            out.append((t, f"step={k} target={target} got={x - 21},{y + 37} result=pass"))
        res = "timeout" if tp_fail else "pass"
        out.append((t, f"test=tp result={res} ms={int((t - t0) * 1000)}"))
        return out, t


class Operator:
    """The Kode's side of the operator steps when a host injects the presses
    and the touches: one prompt at a time, a clean press passes on release, a
    wrong one asks again, nothing in 20 s is a timeout."""

    def __init__(self, emit):
        self.emit = emit                      # emit(record) into the poll ring, now
        self.steps = [("press", b) for b in BUTTONS] + [("target", t) for t in TARGETS]
        self.idx = -1
        self.t_prompt = 0.0
        self.t_start = 0.0
        self.timed_out = {"buttons": False, "tp": False}
        self.down = None
        self.done = False

    def start(self, now):
        self.t_start = now
        self.next(now)

    def kind(self):
        return self.steps[self.idx][0]

    def number(self):
        if self.idx < len(BUTTONS):
            return self.idx + 1, len(BUTTONS)
        return self.idx - len(BUTTONS) + 1, len(TARGETS)

    def prompt(self, now):
        k, of = self.number()
        kind, what = self.steps[self.idx]
        self.t_prompt = now
        self.emit(f"step={k} of={of} {kind}={what}")

    def next(self, now):
        self.idx += 1
        if self.idx == len(BUTTONS):
            self.emit(f"test=buttons result={self.word('buttons')} ms={int((now - self.t_start) * 1000)}")
            self.t_start = now
        if self.idx == len(self.steps):
            self.emit(f"test=tp result={self.word('tp')} ms={int((now - self.t_start) * 1000)}")
            self.done = True
            return
        self.prompt(now)

    def word(self, test):
        """The verdict as the Kode words it (test_dot_ao.c, op_word()): this
        model forgives every wrong press and miss, so a step that ran out is
        the only way it fails, and that is a timeout."""
        if self.timed_out[test]:
            return "timeout"
        return "pass"

    def tick(self, now):
        if self.done or self.idx < 0 or now - self.t_prompt < OPERATOR_LIMIT_S:
            return
        k, _ = self.number()
        kind, what = self.steps[self.idx]
        if kind == "press":
            self.emit(f"step={k} button={what} result=timeout")
            self.timed_out["buttons"] = True
        else:
            self.emit(f"step={k} target={what} result=timeout")
            self.timed_out["tp"] = True
        self.next(now)

    def button(self, name, state, now):
        if self.done or self.idx < 0 or self.kind() != "press":
            return
        k, _ = self.number()
        want = self.steps[self.idx][1]
        if state == "press" and name != want:
            self.emit(f"step={k} button={want} result=wrong pressed={name} "
                      f"ms={int((now - self.t_prompt) * 1000)}")
            self.prompt(now)
        elif state == "release" and name == want:
            self.emit(f"step={k} button={want} result=pass ms={int((now - self.t_prompt) * 1000)}")
            self.next(now)

    def touch(self, x, y, state, now):
        if self.done or self.idx < 0 or self.kind() != "target":
            return
        if state == "down":
            self.down = (x, y)
            return
        if state != "up" or self.down is None:
            return
        k, _ = self.number()
        target = self.steps[self.idx][1]
        tx, ty = (int(v) for v in target.split(","))
        gx, gy = self.down
        self.down = None
        if abs(gx - tx) <= TOUCH_PX and abs(gy - ty) <= TOUCH_PX:
            self.emit(f"step={k} target={target} got={gx},{gy} result=pass")
            self.next(now)
        else:
            self.emit(f"step={k} target={target} got={gx},{gy} result=miss")
            self.prompt(now)


class FakeKode:
    """The pyserial object kcli's Board drives, with a Kode Dot behind it.

    Every line the host sends is kept in `lines`; every relay byte the station
    was asked for in `relays_sent`. Opening the port reboots the board, as the
    real one does: the console answers only BOOT_S later.
    """

    def __init__(self, scenario=None, clock=None, speed=1.0):
        self.sc = scenario or Scenario()
        self.clock = clock or RealClock()
        self.speed = float(speed)
        self.timeout = 0.2
        self.port = None
        self.baudrate = None
        self.dtr = None
        self.rts = None
        self.is_open = False
        self.lines = []
        self.relays_sent = []
        self.out = []                 # [(due, seq, bytes)], sorted
        self.seq = itertools.count()
        self.rx = b""
        self.ready_at = 0.0
        self.locked = False           # stored by an earlier session: every station verb refused
        self.locked_now = False       # locked in this session: only `test_dot_end` answers
        self.mode = "dormant"         # test_dot_info's state=: dormant or awake (waking and ending are instants here)
        self.relays = 0xFF
        self.boost = False
        self.tag_text = None
        self.tag_id = None
        self.ring = []                # [(due, seq, record)], sorted: what the poll hands over
        self.run_state = "none"       # test_dot_info's run=: none, running, done
        self.lane_idle_at = None
        self.run_end_at = None
        self.operator = None
        self.photo_asks = {}
        self.photo_sends = {}

    # ---- the pyserial surface --------------------------------------------
    def open(self):
        self.is_open = True
        self.out.clear()
        self.rx = b""
        self.ring.clear()
        self.run_state = "none"
        self.mode = "dormant"
        self.locked = self.locked or self.locked_now   # the reboot reads the stored lock
        self.locked_now = False
        self.ready_at = self.now() + BOOT_S / self.speed
        self.at(0.05, "!I 2026-11-03T16:28:34.787Z klog kodeOS logging up (ESP-IDF stream captured)")
        self.at(BOOT_S - 0.1, "!I 2026-11-03T16:28:34.857Z kcli console up ('help' for commands)")

    def close(self):
        self.is_open = False

    @property
    def in_waiting(self):
        now = self.now()
        return sum(len(b) for d, _, b in self.out if d <= now)

    def write(self, data):
        self.rx += bytes(data)
        while b"\n" in self.rx:
            raw, self.rx = self.rx.split(b"\n", 1)
            line = raw.decode(errors="replace").strip("\r")
            if line:
                self.lines.append(line)
                if self.now() >= self.ready_at:
                    self.handle(line)
        return len(data)

    def flush(self):
        pass

    def read(self, n=1):
        now = self.now()
        if not self.out or self.out[0][0] > now:
            due = self.out[0][0] if self.out else now + self.timeout
            self.clock.wait_until(due, self.timeout)
            now = self.now()
        got = b""
        while self.out and self.out[0][0] <= now and len(got) < max(n, 1):
            got += self.out.pop(0)[2]
        return got

    # ---- helpers -----------------------------------------------------------
    def now(self):
        return self.clock.monotonic()

    def at(self, delay, line):
        """Say `line` `delay` seconds from now (scaled by the speed)."""
        due = self.now() + delay / self.speed
        if not line.startswith((">", "!", "kode>")):
            line = ">|" + line
        bisect.insort(self.out, (due, next(self.seq), (line + "\r\n").encode()))

    def answer(self, records, term=">ok", delay=0.02):
        for r in records:
            self.at(delay, r)
        self.at(delay, term)

    def ring_add(self, delay, record):
        bisect.insort(self.ring, (self.now() + delay / self.speed, next(self.seq), record))

    # ---- the console ---------------------------------------------------------
    def handle(self, line):
        self.at(0.0, "kode> " + line)
        verb, _, rest = line.partition(" ")
        args = fields(rest)
        if verb.startswith("test_dot_") or (verb == "test" and args.get("component") == "all"):
            if not self.sc.has_test_dot:
                if verb == "test":
                    self.answer([], ">err code=5 msg=not_built")
                else:
                    self.answer([], f">err code=1 msg={verb}")
                return
            if self.locked or (self.locked_now and verb != "test_dot_end"):
                self.answer([], ">err code=5 msg=locked")
                return
            if verb in WAKING_VERBS:
                self.mode = "awake"
        fn = getattr(self, "v_" + verb, None)
        if fn is None:
            self.answer([], f">err code=1 msg={verb}")
            return
        fn(args)

    def v_ping(self, a):
        self.answer(["pong"])

    def v_info(self, a):
        recs = list(INFO)
        recs[0] = recs[0].replace(SERIAL, self.sc.serial)
        self.answer(recs)

    def v_test_dot_info(self, a):
        self.answer([f"test_dot state={self.mode} locked={int(self.locked_now)} run={self.run_state} "
                     "psram_free=27853504 j3=free"])

    def v_test_dot_screen(self, a):
        self.answer([f"screen phase={a.get('phase', '-')} ok=1"])

    def v_test_dot_place(self, a):
        for d, r in self.sc.place():
            self.at(d, r)

    def v_test(self, a):
        comp = a.get("component")
        if comp == "all":
            self.start_run()
            return
        if comp == "pmic":
            self.at(0.01, "!N 2026-11-03T16:30:00.000Z kaos.power test pmic: charge=fast input=usb_dcp "
                          "pg=1 vbus_gd=1 vsys_min=3500 faults=0x00 ntc=normal")
        elif comp == "gauge":
            self.at(0.01, "!N 2026-11-03T16:30:00.000Z kaos.power test gauge: vcell 4012mV current 500mA "
                          "ave 480mA (into the cell positive) temp 25.3C")
        self.answer([f"test={comp} result=pass ms=4"], delay=0.05)

    def start_run(self):
        self.run_state = "running"
        for d, r in self.sc.run_line():
            self.at(d, r)
        cam_end = max(d for d, _ in self.sc.run_line()) + 0.05
        self.at(cam_end, ">ok")
        for d, r in self.sc.run_ring():
            self.ring_add(d, r)
        self.lane_idle_at = self.now() + max(d for d, _ in self.sc.run_ring()) / self.speed
        if self.sc.operator == "injected":
            self.operator = Operator(lambda r: self.ring_add(0.0, r))
            self.pending_operator_at = self.now() + (cam_end + 0.2) / self.speed
            self.run_end_at = None
        else:
            script, end = self.sc.operator_script()
            for d, r in script:
                self.ring_add(cam_end + d, r)
            self.run_end_at = self.now() + (cam_end + end + 0.1) / self.speed
            self.ring_add(cam_end + end + 0.1, f"timing=all total_ms={int((cam_end + end) * 1000)} "
                                               f"cam_end_ms={int(cam_end * 1000)} sd_ms=2098 imu_ms=629")

    def v_test_dot_poll(self, a):
        now = self.now()
        op = self.operator
        if op is not None:
            if op.idx < 0 and now >= self.pending_operator_at:
                op.start(now)
            op.tick(now)
            if op.done and self.run_end_at is None:
                self.run_end_at = now
                self.ring_add(0.0, "timing=all total_ms=30000 cam_end_ms=12050 sd_ms=2098 imu_ms=629")
        take = [x for x in self.ring if x[0] <= now][:POLL_MAX]
        for x in take:
            self.ring.remove(x)
        due = [x[2] for x in take]
        done = (not self.ring) and self.run_end_at is not None and now >= self.run_end_at
        if done:
            self.run_state = "done"
        lane = "idle" if (self.lane_idle_at is not None and now >= self.lane_idle_at) else "busy"
        self.answer(due + [f"poll state={'done' if done else 'running'} lane={lane}"])

    def v_btn(self, a):
        name = BTN_IDS.get(a.get("id"))
        if name is None or a.get("state") not in ("press", "release", "hold", "double"):
            self.answer([], ">err code=3 msg=id")
            return
        if self.operator is not None:
            self.operator.button(name, a["state"], self.now())
        self.answer([])

    def v_touch(self, a):
        try:
            x, y = int(a["x"]), int(a["y"])
        except (KeyError, ValueError):
            self.answer([], ">err code=3 msg=x")
            return
        if self.operator is not None:
            self.operator.touch(x, y, a.get("state"), self.now())
        self.answer([])

    def v_test_dot_relays(self, a):
        raw = a.get("state", "")
        try:
            b = int(raw, 16)
        except ValueError:
            self.answer([], ">err code=3 msg=state")
            return
        if self.sc.case == "fail":
            self.answer([f"relays state=0x{b:02X} ok=0"], ">err code=4 msg=station_i2c", delay=0.03)
            return
        if (b & 0x03) == 0x01:
            self.answer([], ">err code=5 msg=forbidden")
            return
        self.relays_sent.append(b)
        self.relays = b
        self.answer([f"relays state=0x{b:02X} ok=1"])

    def volts(self, ch):
        if self.relays == 0xFC:
            return {0: 4.64 if self.boost else 0.0, 1: 1.63, 2: 0.0, 3: 5.03}[ch]
        return GOOD_VOLTS.get((self.relays, ch), 0.0)

    def v_test_dot_adc(self, a):
        ch = int(a.get("ch", "0"))
        mv = int(round(self.volts(ch) * 1000 / (1 if ch == 1 else 2)))
        self.answer([f"adc ch={ch} mv={mv}"], delay=0.01)

    def v_test_dot_boost(self, a):
        self.boost = a.get("state") == "on"
        cur = -1150 if self.boost else -20
        self.answer([f"boost on={1 if self.boost else 0} fault=0x00 current_ma={cur}"], delay=0.3)

    def v_test_dot_loop(self, a):
        n = int(a.get("count", "20"))
        self.answer([f"loop ok=1 b9600={n}/{n} b115200={n}/{n} rev={n}/{n} ms=410 why=-"], delay=0.4)

    def v_bat_info(self, a):
        if self.relays == 0xFE:
            rec = "vbat_mv=4000 vbus_mv=4550 vbus=on boost=off current_ma=514 soc=80 charging=1 fault=0"
        elif self.relays == 0xFC:
            rec = (f"vbat_mv=3900 vbus_mv=0 vbus=off boost={'on' if self.boost else 'off'} "
                   f"current_ma={-1150 if self.boost else -20} soc=80 charging=0 fault=0")
        else:
            rec = "vbat_mv=4000 vbus_mv=4900 vbus=on boost=off current_ma=516 soc=80 charging=1 fault=0"
        self.answer([rec], delay=0.05)

    def v_temp(self, a):
        self.answer(["temp=p4 c=41.3"])

    def v_test_dot_radio(self, a):
        recs = ["radio_card ok=1 chip=esp32c5 id=23 kode=1 fw=2.12.9 host=2.12.9 "
                "mac=38:44:be:07:e6:55 sdio=4x40000kHz",
                "radio_time card_ms=120 w24_ms=500 w5_ms=900 ble_ms=1000 ms=2600",
                "radio ok=1 seen24=1 rssi24=-48 ch24=2 seen5=1 rssi5=-55 ch5=48 passive5=0 "
                "ble_adv=1 ble_rx=1 rssi_rx=-61 why=-"]
        self.answer(recs, delay=2.6)

    def v_nfc_write(self, a):
        if self.sc.case in ("fail", "nfc"):
            self.answer(["nfc=tag state=none"], '>err code=4 msg="no tag in the window"', delay=3.0)
            return
        self.tag_text = a.get("text")
        self.answer(['nfc=tag uid=04A1B2C3D4E5F6 atqa=0x0044 sak=0x00 type=2 model="NTAG213"'], delay=0.8)

    def v_nfc_read(self, a):
        if self.sc.case in ("fail", "nfc") or self.tag_text is None:
            self.answer(["nfc=tag state=none"], '>err code=4 msg="no tag in the window"', delay=3.0)
            return
        self.answer(['nfc=tag uid=04A1B2C3D4E5F6 atqa=0x0044 sak=0x00 type=2 model="NTAG213"',
                     f"nfc=ndef len={len(self.tag_text) + 7} text={quote(self.tag_text)}"], delay=0.6)

    def v_rfid_write(self, a):
        self.tag_id = a.get("id")
        self.answer([f"rfid=clone id={quote(self.tag_id)} match=yes"], delay=1.5)

    def v_rfid_read(self, a):
        if self.tag_id is None:
            self.answer(["rfid=tag state=none"], '>err code=4 msg="no tag in the window"', delay=3.0)
            return
        self.answer([f"rfid id={quote(self.tag_id)} rf=64"], delay=0.7)

    def photo(self, n):
        """Photo n's lines, or none when the scenario loses it this time."""
        name = f"photo_{n:02d}_{PHOTOS[n - 1]}"
        sent = self.photo_sends.get(n, 0)
        self.photo_sends[n] = sent + 1
        if sent < self.sc.photo_missing.get(n, 0):
            return []
        data = make_jpeg(name)
        crc = None
        if sent < self.sc.photo_bad_crc.get(n, 0):
            crc = (zlib.crc32(data) ^ 0x5A5A5A5A) & 0xFFFFFFFF
        return blob_lines(name, data, crc)

    def v_test_dot_photos(self, a):
        only = a.get("only")
        wanted = [int(only)] if only else list(range(1, len(PHOTOS) + 1))
        recs, n, size = [], 0, 0
        if not only:                   # the total comes with the whole set only
            recs.append(f"photos total={len(PHOTOS)}")
        for k in wanted:
            lines = self.photo(k)
            if lines:
                n += 1
                size += int(fields(lines[0][2:])["len"])
            recs += [x[2:] for x in lines]
        recs.append(f"photos n={n} bytes={size} ms=139")
        self.answer(recs, delay=0.1)

    def v_test_dot_blob(self, a):
        name = a.get("name")
        if name == "location":
            self.answer([x[2:] for x in blob_lines(name, make_location())])
        elif name == "colours":
            self.answer([x[2:] for x in blob_lines(name, make_colours())])
        else:
            self.answer([], ">err code=3 msg=name")

    def v_test_dot_result(self, a):
        passing = a.get("verdict") == "pass"
        if (passing and self.run_state != "done") or (not passing and self.run_state == "none"):
            self.answer([], ">err code=4 msg=no_run")
            return
        # A pass runs the factory reset first; the line answers when it is done.
        self.answer([], delay=6.0 if a.get("verdict") == "pass" else 0.1)

    def v_test_dot_lock(self, a):
        if self.sc.lock_refused:
            self.answer([], ">err code=4 msg=nvs")
            return
        self.locked_now = True
        self.answer([])

    def v_test_dot_end(self, a):
        self.run_state = "none"
        self.mode = "dormant"
        self.answer([], delay=0.3)


def seed_config(path, template):
    """A station's settings as the bench has them: the template with the
    recorded networks' channels (2.4 GHz on 2, 5 GHz on 48)."""
    import json
    path = Path(path)
    if not path.exists():
        cfg = json.loads(Path(template).read_text())
        cfg["wifi"]["channel_24"], cfg["wifi"]["channel_5"] = 2, 48
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(cfg, indent=2) + "\n")


def install(fake, final):
    """Point `final` (the test_dot_final module) at `fake` instead of a port."""
    final.kcli.serial.Serial = lambda: fake
    final.port_present = lambda port: fake.sc.case != "no_usb"
    final.port_holders = lambda port: []
    final.make_beacon = lambda name: NullBeacon()


class NullBeacon:
    """The station's BLE beacon, minus the radio."""

    def on(self):
        return dict(beacon=True, error=None)

    def off(self):
        pass


def main(argv=None):
    ap = argparse.ArgumentParser(description="The station program against a fake Kode.")
    ap.add_argument("--case", default=os.environ.get("TEST_DOT_CASE", "pass"),
                    choices=["pass", "fail", "nfc", "buttons", "noverdict", "timeouts", "misplaced", "no_camera", "no_usb"])
    ap.add_argument("--speed", type=float, default=float(os.environ.get("TEST_DOT_SPEED", "1")),
                    help="how much faster than the recorded board the fake answers")
    ap.add_argument("--events", action="store_true")
    ap.add_argument("--operator-sim", action="store_true")
    ap.add_argument("--no-open", action="store_true")
    ap.add_argument("--out", default=str(Path("~/kode-factory/sim-reports").expanduser()))
    ap.add_argument("--config", default=None)
    ap.add_argument("-p", "--port", default=PORT)
    a = ap.parse_args(argv)
    if a.speed <= 0:
        ap.error("--speed must be above 0")

    try:
        import serial  # noqa: F401
    except ImportError:                     # the fake stands in for the port; kcli only needs the names
        import types
        stub = types.ModuleType("serial")
        stub.Serial = None
        stub.SerialException = type("SerialException", (OSError,), {})
        sys.modules["serial"] = stub
    import test_dot_final as final
    sc = Scenario(a.case, operator="injected" if a.operator_sim else "recorded")
    fake = FakeKode(sc, RealClock(), a.speed)
    install(fake, final)
    out = Path(a.out).expanduser()
    out.mkdir(parents=True, exist_ok=True)
    cfg = a.config or str(out / "test_dot_station.json")
    seed_config(cfg, final.TEMPLATE)
    final.USB_WAIT_OVERRIDE_S = 4.0 if a.case == "no_usb" else None
    final.REMOVAL_AFTER_S = 4.0       # the simulated operator unplugs the Kode 4 s after the end
    args = ["--port", a.port, "--out", str(out), "--config", cfg]
    args += ["--events"] if a.events else []
    args += ["--operator-sim"] if a.operator_sim else []
    args += ["--no-open"] if a.no_open else []
    return final.main(args)


if __name__ == "__main__":
    sys.exit(main())
