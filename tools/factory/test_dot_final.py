#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The E5 station's final test of a Kode Dot, end to end, one command.

    python3 tools/factory/test_dot_final.py [--port P] [--events] [--no-place] [--no-write]
                                            [--operator-sim] [--discover] [--out DIR]
                                            [--config FILE] [--no-open]

The station's PC drives the Kode over the USB-C console, in ONE open session
(opening the port reboots the board, which would drop the PASS screen and the
armed ship mode, both in RAM):

  1. Identify the Kode (`info`) and check its kodeOS speaks the station verbs
     (`test_dot_info`); refuse a kodeOS that does not, saying why.
  2. Placement (`test_dot_place`): the camera on J3 finds the four marks.
  3. The run (`test component=all`): the Kode's rows, the camera and its LED,
     the operator's buttons and touches. The line comes back at the camera's
     end; the rest is read with `test_dot_poll`, and meanwhile the PC does its
     own part: the rear pads (`test_dot_relays`, `test_dot_adc`,
     `test_dot_loop`, `test_dot_boost`), NFC and RFID written and read back,
     the radio (`test_dot_radio`, with the Mac's BLE beacon).
  4. The exit record (battery, charger, temperatures, versions).
  5. The verdict, which the PC decides and tells the Kode
     (`test_dot_result`): PASS runs the Kode's factory reset.
  6. The photos (`test_dot_photos`, every one checked by its CRC and asked
     again up to three times) and, on a PASS, the lock (`test_dot_lock`). A
     lock the Kode does not confirm turns the run into a FAIL (exit 3).
  7. The report, the station's CSV and the `end` event, all after the lock.

Reports go under --out (default ~/kode-factory/reports), one folder per run,
`<date>_<serial>/`, with the station's CSV beside them; nothing is written
into the tree. The station's settings are a copy of test_dot_station.json
(--config, default ~/kode-factory/test_dot_station.json), made on first use.

--events prints one `@@TD {json}` line per event for the station's page
(ui/test_dot_ui.py; the contract is ui/EVENTS.md). --operator-sim is the
bench's operator: it presses and touches through the console's `btn` and
`touch` injection. --no-write makes NFC and RFID read only; --no-place skips
the placement. Any of the three makes a bench run: the verdict reads BENCH,
never joins the CSV, and the Kode is never reset nor locked. --discover finds the channels of the
station's two networks once and keeps them in the settings.

The wire is section 7 of the E5 spec; the station and its page are
https://docs.kode.diy (the station's page of the kodeOS section).
"""

import argparse
import base64
import csv
import html
import io
import json
import os
import random
import re
import shutil
import signal
import statistics
import sys
import time
import traceback
import types
import webbrowser
import zlib
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
for _p in (str(TOOLS), str(HERE)):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import kcli  # noqa: E402
from kpayload import fields, quote  # noqa: E402

EVENT_PREFIX = "@@TD"
TEMPLATE = HERE / "test_dot_station.json"
HOME = Path("~/kode-factory")
DEFAULT_PORT = "/dev/cu.usbmodem101"

# Time, swappable: the tests run the whole station on a virtual clock.
CLOCK = types.SimpleNamespace(monotonic=time.monotonic, sleep=time.sleep, time=time.time)

USB_WAIT_OVERRIDE_S = None     # the simulator's shorter wait for a Kode that never comes
REMOVAL_AFTER_S = None         # the simulator's operator unplugs this long after the end
REMOVAL_WAIT_S = 900           # how long the end waits for the Kode to leave the USB
REPEAT_WINDOW_S = 1800         # the same Kode back within this, after no PASS, is a repeat
OPERATOR_SIM_SEED = None       # None: a new operator every run

PING_TRIES = 20
PING_TIMEOUT_S = 2.0
RUN_BUDGET_S = 300             # the whole run, camera and operator included
POLL_PERIOD_S = 0.15
POLL_TIMEOUT_S = 5.0
CAMERA_EXPECTED_S = 8          # the page's "do not touch" countdown
STATION_FAULT = "station: "    # a row the station could not measure says so first
OPERATOR_LIMIT_S = 20          # the Kode gives each button and touch step this long
# The rear pads' limits, Luismi's from the station's bench (e5_test.py),
# read at the point the ADC measures (x2 divider undone, A1 direct).
PC_5V_MIN_V = 4.5              # A3 is the PC's USB 5 V that feeds the relays and the load: below, no station
POGO_5V_MIN_V = 4.0            # A0 at rest: the +5V pogo touches the Kode's pad (and a charge that sags below cut)
GND_PAD_MAX_V = 2.2            # A1 with the GND relay: the Kode's pad pulls the 3.3 V pull-up down
GND_OPEN_MIN_V = 3.0           # A1 near the pull-up: the GND pogo touches nothing; between the two, doubtful
CHARGE_A2_MAX_V = 0.8          # A2 while charging: the return path is on the Kode's ground
CHARGE_A0_MIN_V = 4.4          # A0 while charging: the 5 V reaches the pad with the charger's load on it
CHARGE_VBUS_MIN_MV = 4300      # the BQ25896 sees VBUS: inside its input range, under its 5.3 V overvoltage
CHARGE_VBUS_MAX_MV = 5300
DELIVERY_IDLE_MAX_V = 0.5      # A0 with the load and no boost: nothing back-feeds the pad
DELIVERY_A0_MIN_V = 4.4        # A0 with the boost into 6.6 ohm (about 0.71 A): the boost holds 5 V
DELIVERY_A0_MAX_V = 5.25
DELIVERY_CURRENT_MIN_MA = -2500   # the gauge discharging into the load: the boost's draw from the cell,
DELIVERY_CURRENT_MAX_MA = -300    # at least a real load, at most the cell's 2 A plus margin
LOCK_TIMEOUT_S = 10.0          # `test_dot_lock` writes one NVS key
LOCK_OK, LOCK_FAILED, LOCK_NOT_SENT = "ok", "failed", "not_sent"
LOCK_STEP_NAME = "The station verbs locked"
EXIT_PASS = 0                  # the core verdict is PASS (a bench PASS too)
EXIT_NOT_PASS = 1              # FAIL or NO VERDICT, report written
EXIT_NO_REPORT = 2             # refused before anything was written
EXIT_LOCK_FAILED = 3           # PASS everywhere but the lock: the Kode must not leave
GLASS_W_PX = 410               # the console's touch points: x 0..409, y 0..501
GLASS_H_PX = 502
SIM_MISS_PX = 70               # the bench operator's touch off the target
SIM_JITTER_PX = 8              # and its scatter on the target
# The station's waits sit above the Kode's own bounds (kqp_config.h), so the
# Kode's answer, a timeout of its own included, always arrives before the PC
# gives up: the Kode's bound < its console's line < the PC's wait.
MS_PER_S = 1000.0              # milliseconds in a second, for the Kode's times
WAIT_MARGIN_S = 5.0            # what the PC adds to a Kode bound: the line's way over USB and back
KODE_RESET_S = 290.045         # the SHIP factory reset at every step's budget (KODE_RESET_CONSOLE_MS)
KODE_LINE_MARGIN_S = 5.0       # what the Kode's line adds to the reset's bound (KODE_TEST_DOT_LINE_MARGIN_MS)
KODE_LINE_S = KODE_RESET_S + KODE_LINE_MARGIN_S   # the Kode's console gives a station verb's line this long (KODE_TEST_DOT_LINE_MS)
RESULT_PASS_TIMEOUT_S = KODE_LINE_S + WAIT_MARGIN_S   # `test_dot_result verdict=pass` answers when the reset is done
KODE_ANSWER_S = 5.0            # the longest an owner's answer may take (KODE_TEST_DOT_ANSWER_MS)
KODE_JOB_S = 20.0              # the longest one worker job may take (KODE_TEST_DOT_JOB_MS)
KODE_PROBE_S = 8.0             # the radio check's budget between its jobs (KODE_RADIO_PROBE_MS)
KODE_RADIO_QUEUE_S = 25.0      # what the radio check may wait behind AO_Radio's running job (KODE_TEST_DOT_RADIO_QUEUE_MS)
KODE_RADIO_S = KODE_PROBE_S + KODE_RADIO_QUEUE_S   # the radio check's bound (KODE_TEST_DOT_RADIO_MS)
RADIO_TIMEOUT_S = KODE_RADIO_S + WAIT_MARGIN_S
KODE_SWEEP_S = 102.0           # a whole Wi-Fi sweep at its driver's bound (KODE_TEST_DOT_SWEEP_MS)
KODE_DISCOVER_MARGIN_S = 5.0   # what the discovery's bound adds (KODE_TEST_DOT_DISCOVER_MARGIN_MS)
# The discovery: the radio check's bound, the sweep, the budget counted again after it, the margin (KODE_TEST_DOT_DISCOVER_MS).
KODE_DISCOVER_S = KODE_RADIO_S + KODE_SWEEP_S + KODE_PROBE_S + KODE_DISCOVER_MARGIN_S
DISCOVER_TIMEOUT_S = KODE_DISCOVER_S + WAIT_MARGIN_S
# The pads loop: the dock's pins lent, the frames' job, the pins back (KODE_TEST_DOT_LOOP_MS).
KODE_LOOP_S = KODE_ANSWER_S + KODE_JOB_S + KODE_ANSWER_S
LOOP_TIMEOUT_S = KODE_LOOP_S + WAIT_MARGIN_S
# The end: the boost off, a job still out, the dock's pins back, the camera's close job (KODE_TEST_DOT_END_MS).
KODE_END_S = KODE_ANSWER_S + KODE_JOB_S + KODE_ANSWER_S + KODE_JOB_S
END_TIMEOUT_S = KODE_END_S + WAIT_MARGIN_S
PHOTO_ROUNDS = 3
BLOB_TRIES = 3
REPORT_DETAIL_CHARS = 6000

# id -> (name, block)
STEPS = {
    "place": ("Placing the Kode in the fixture", "placement"),
    "info": ("Identifying the Kode", "automatic"),
    "pmic": ("Charger (BQ25896)", "automatic"),
    "gauge": ("Battery gauge", "automatic"),
    "ioexp": ("Input expander", "automatic"),
    "clocks": ("Crystals", "automatic"),
    "imu": ("IMU", "automatic"),
    "mag": ("Compass", "automatic"),
    "haptic": ("Vibration", "automatic"),
    "audio": ("Audio", "automatic"),
    "sd": ("microSD", "automatic"),
    "nfc": ("NFC", "automatic"),
    "rfid": ("RFID 125 kHz", "automatic"),
    "ir": ("Infrared", "automatic"),
    "c5": ("C5 coprocessor: card", "automatic"),
    "wifi_24": ("Wi-Fi 2.4 GHz radio: reception", "automatic"),
    "wifi_5": ("Wi-Fi 5 GHz radio: reception", "automatic"),
    "ble_rx": ("BLE radio: reception", "automatic"),
    "cam_framing": ("Camera: location and framing", "camera"),
    "cam_pixels": ("Camera: dead and lit pixels", "camera"),
    "cam_lines": ("Camera: lines", "camera"),
    "cam_zones": ("Camera: dark zones", "camera"),
    "cam_colour": ("Camera: colour", "camera"),
    "cam_straight": ("Camera: screen straight", "camera"),
    "led": ("Red, green and blue LED", "led"),
    "pads_charge": ("Rear pads: charging", "pads"),
    "pads_delivery": ("Rear pads: delivery", "pads"),
    "pads_gnd": ("Rear pads: GND pad", "pads"),
    "pads_loop": ("Rear pads: data loop", "pads"),
    "buttons": ("Buttons", "operator"),
    "tp": ("Touch", "operator"),
    "mem": ("Internal memory", "memory"),
    "exit_record": ("The Kode's exit record", "record"),
}
FACTORY_RESET = ("factory_reset", "Factory reset", "record")   # a step only once a PASS is sent
CERTIFICATE = ("certificate", "Certificate", "record")        # a step only before a PASS, where one is issued
CERTIFICATE_RANGE = "factory-link issues it and the C5 keeps it"

# The Kode's certificate, asked before a PASS reaches the Kode, whose factory
# reset it runs. None: this station issues none. A station that does sets it
# to a callable (link, station) -> the row's words, which raises
# CertificateRefused, the reason as its message, when the Kode gets none.
certify = None
ORDER = list(STEPS)
CAM_IDS = ["cam_framing", "cam_straight", "cam_pixels", "cam_lines", "cam_zones", "cam_colour"]
RADIO_IDS = ["c5", "wifi_24", "wifi_5", "ble_rx"]
PADS_IDS = ["pads_charge", "pads_delivery", "pads_gnd", "pads_loop"]
# The Kode's rows, in the order a run gives them (sd and imu with the camera,
# audio and haptic after its photos, the rest beside the operator, mem last).
ROW_SEQ = ["sd", "imu", "audio", "haptic", "mag", "pmic", "gauge", "ioexp", "clocks", "ir", "nfc", "rfid", "mem"]
RUN_IDS = ROW_SEQ + CAM_IDS + ["led", "buttons", "tp"]
BACKGROUND = ["pads", "nfc", "rfid", "radio"]     # the PC's work beside the operator, longest first
# Steps that are run and reported in full but do not decide the verdict.
OPTIONAL = {"nfc"}

PASS, FAIL, TIMEOUT, NOVERDICT, NOTRUN = "PASS", "FAIL", "TIMEOUT", "NO VERDICT", "NOT RUN"
EVENT_STATE = {NOVERDICT: "NOVERDICT", NOTRUN: "NOT_RUN"}
ROW_WORD = {"pass": PASS, "fail": FAIL, "timeout": TIMEOUT, "skipped": NOVERDICT, "busy": NOVERDICT,
            "absent": FAIL, "noverdict": NOVERDICT}
CAM_WORD = {"pass": PASS, "fail": FAIL, "noverdict": NOVERDICT}

PHASE_TEXT = {
    "place": "Put the Kode in the nest",
    "camera": "DO NOT TOUCH: the camera is looking at the screen",
    "buttons": "Press the button that lights up",
    "touch": "Touch the circle that lights up",
    "rest": "Finishing the automatic tests",
    "result": "Result",
}
PLACE_TEXT = {"searching": "Looking for the marks",
              "no_camera": "Camera not detected: check the camera board",
              "misplaced": "Kode badly seated: adjust it in the fixture"}
# A press that was not clean asks for the same step again; these end it.
OPERATOR_REPEAT = {"wrong": "other_button", "double": "double"}
OPERATOR_FAIL = {"doublefail": "double", "stuck": "no_release", "held": "held", "giveup": "mistakes"}
# The injection verb's names for the buttons the Kode asks for.
INJECT_ID = {"pad-left": "left", "bottom": "bot", "pad-down": "down", "top": "top",
             "pad-right": "right", "pad-up": "up"}

PLACE_DEFAULTS = dict(usb_s=30, camera_s=10, marks_s=15, dx_mm=15, turn_deg=5, window=[704, 628])

# The rear pads: the station's relays and ADC, through the Kode.
RELAY_REST = 0xFF          # every relay open
RELAY_GND = 0xFB           # the GND pad pulled to the station's ground
RELAY_CHARGE = 0xFE        # the station's 5 V into the Kode's pads
RELAY_DELIVERY = 0xFC      # the Kode's boost into the station's load
RELAY_FORBIDDEN_MASK = 0x03
RELAY_FORBIDDEN = 0x01     # R2 closed with R1 at rest: the PC's 5 V into the load through the Kode
RELAY_SETTLE_S = 0.02
GND_SETTLE_S = 0.1
CHARGE_SETTLE_S = 1.5
DELIVERY_SETTLE_S = 0.5
BOOST_SETTLE_S = 0.6
LOOP_COUNT = 20
ADC_DIRECT_CH = 1          # A1 is wired straight; the others sit behind a /2 divider

# The colour strip of the report: the Kode's `colours` blob, five crops.
STRIP_ORDER = ["red", "green", "blue", "white", "grey"]
STRIP_TILE = (330, 220)
STRIP_CAPTION_H = 46
STRIP_HEADER_H = 60
STRIP_GAP = 8
STRIP_COLS = 3
STRIP_TOL_DEFAULT = 0.015
STRIP_OK = (60, 170, 60)
STRIP_BAD = (220, 40, 40)

# The exit record: how the Kode leaves the station. Adding a field is adding
# it here and its reading in Station.read_exit(); a changed header sets the
# old CSV aside with a date rather than mixing headers.
EXIT_FIELDS = [
    ("date_time", "Date and time"), ("station", "Station"), ("serial", "Serial"), ("mac_p4", "MAC P4"),
    ("mac_c5", "MAC C5 (Wi-Fi)"), ("kodeos", "kodeOS"), ("c5", "C5 (app)"), ("c5_fw", "C5 (ESP-Hosted firmware)"),
    ("cell_mv", "Cell voltage (mV)"), ("soc_pct", "SoC (%)"), ("current_ma", "Cell current (mA)"),
    ("charger", "Charger state"), ("input", "Power input"), ("vbus_mv", "VBUS (mV)"),
    ("charger_faults", "Charger faults"), ("temp_p4_c", "P4 temperature (C)"), ("temp_cell_c", "Cell temperature (C)"),
    ("duration_s", "Total time (s)"), ("verdict", "Verdict"), ("lock", "Station verbs locked"),
]
COLOURS = {PASS: "#31C45A", FAIL: "#C9303D", TIMEOUT: "#E0A020", NOTRUN: "#888888", NOVERDICT: "#E07820"}


def port_present(port):
    """True while the Kode's port exists (the Kode is on the USB)."""
    return os.path.exists(port)


def port_holders(port):
    """PIDs holding the port open (kcli's lsof check)."""
    return kcli.port_holders(port)


def make_beacon(name):
    """The Mac's BLE beacon (test_dot_ble.py, macOS only)."""
    import test_dot_ble
    return test_dot_ble.Beacon(name)


class Abort(Exception):
    """The run cannot go on; the reason is the message."""


class PadsError(Exception):
    """The station's relays or ADC said no, or a relay byte is refused."""


class CertificateRefused(Exception):
    """No certificate for this Kode; the reason is the message (see `certify`)."""


class CommandError(Exception):
    """A console line answered `>err`, or nothing in time."""

    def __init__(self, line, term, records=(), logs=()):
        self.line, self.term = line, term
        self.records, self.logs = list(records), list(logs)
        f = fields(term[4:]) if term else {}
        self.code = int(f["code"]) if f.get("code", "").isdigit() else None
        self.msg = f.get("msg", "")
        super().__init__(f"{line}: {term or 'no answer'}")


def relay_guard(prev, byte):
    """Refuse a relay byte before it reaches the wire.

    R2 closed with R1 at rest is never sent (the Kode refuses it too). The
    delivery state is entered only from charging and left only to charging,
    so the load never sees the station's 5 V and the Kode's boost at once.
    """
    if (byte & RELAY_FORBIDDEN_MASK) == RELAY_FORBIDDEN:
        raise PadsError(f"relay byte 0x{byte:02X} refused (R2 closed with R1 at rest)")
    if prev == RELAY_DELIVERY and byte not in (RELAY_DELIVERY, RELAY_CHARGE):
        raise PadsError(f"from 0x{RELAY_DELIVERY:02X} only to 0x{RELAY_CHARGE:02X}, not 0x{byte:02X}")
    if byte == RELAY_DELIVERY and prev not in (RELAY_CHARGE, RELAY_DELIVERY):
        raise PadsError(f"0x{RELAY_DELIVERY:02X} only from 0x{RELAY_CHARGE:02X}")


def truthy(v):
    return str(v).lower() in ("1", "on", "yes", "true")


def fault_ok(v):
    return str(v).lower() in ("0", "0x0", "0x00", "none", "no", "off")


def num_text(v):
    """A number as the console takes it: bare, without a needless `.0`."""
    f = float(v)
    return str(int(f)) if f.is_integer() else str(f)


def record(recs, name):
    """The fields of the first record named `name` (`>|name ...`)."""
    for r in recs:
        if r == name or r.startswith(name + " "):
            return fields(r)
    return None


def values_from(text, extra=()):
    """The report's values from a record's text: every key=number or word."""
    out = list(extra)
    for k, v in re.findall(r"\b([A-Za-z_][\w.]*)=(-?[\d.]+|0x[0-9A-Fa-f]+|\"[^\"]*\"|[A-Za-z_:\-]+)", text or ""):
        if k in ("test", "result", "step", "of", "summary", "pass", "fail", "timeout"):
            continue
        try:
            val = float(v) if "." in v else int(v, 0)
        except ValueError:
            val = v.strip('"')
        out.append(dict(name=k, value=val, unit="", range=""))
        if len(out) >= 24:
            break
    return out


def value(name, v, unit="", rng=""):
    return dict(name=name, value=v, unit=unit, range=rng)


def reason_from(status, measured, detail):
    """Why a step did not pass: the last error the Kode logged, else what was measured."""
    if status == PASS:
        return ""
    if (measured or "").startswith(STATION_FAULT):   # the station's own fault names itself (ui/reports.py)
        return measured[:300]
    errs = re.findall(r"!E [^ ]+ [^ ]+ (.*)", detail or "")
    return (errs[-1] if errs else measured)[:300]


def rfid_id_of(mac):
    """The EM4100 ID (40 bits) the station's T5577 is written with: the low
    five bytes of the P4's MAC, unique per Kode and deterministic."""
    b = [x for x in re.split(r"[:\-]", mac or "") if x]
    return ":".join(x.upper().zfill(2) for x in b[-5:]) if len(b) >= 5 else None


def load_config(path):
    """The station's settings, made from the template on first use."""
    path = Path(path).expanduser()
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(TEMPLATE, path)
    cfg = json.loads(path.read_text())
    place = dict(PLACE_DEFAULTS)
    place.update(cfg.get("place") or {})
    cfg["place"] = place
    return cfg


def save_config(path, cfg):
    path = Path(path).expanduser()
    tmp = path.with_suffix(".part")
    tmp.write_text(json.dumps(cfg, indent=2, ensure_ascii=False) + "\n")
    os.replace(tmp, path)


def write_atomic(path, data):
    """A whole file or none: a SIGTERM never leaves one half written."""
    tmp = Path(str(path) + ".part")
    tmp.write_bytes(data)
    os.replace(tmp, path)


def colour_strip(jpeg, test, path):
    """The report's colour mosaic from the Kode's strip of five crops (red,
    green, blue, white, grey) and its cam_colour fields: each crop with its
    distance and the tolerance, a red frame on the ones over it."""
    from PIL import Image, ImageDraw
    strip = Image.open(io.BytesIO(jpeg)).convert("RGB")
    tol = float(test.get("tol", STRIP_TOL_DEFAULT))
    sw = strip.width // len(STRIP_ORDER)
    tiles, all_ok = [], True
    for i, name in enumerate(STRIP_ORDER):
        d = float(test.get(f"dist_{name}", 0.0))
        ok = d <= tol
        all_ok &= ok
        tile = strip.crop((i * sw, 0, (i + 1) * sw, strip.height)).resize(STRIP_TILE, Image.BICUBIC)
        t = Image.new("RGB", (STRIP_TILE[0], STRIP_TILE[1] + STRIP_CAPTION_H), (245, 245, 245))
        t.paste(tile, (0, 0))
        dr = ImageDraw.Draw(t)
        dr.text((8, STRIP_TILE[1] + 6), name, fill=(30, 30, 30))
        dr.text((8, STRIP_TILE[1] + 26), f"distance {d:.4f}  tolerance {tol:.3f}", fill=(60, 60, 60))
        width = 2 if ok else 4
        dr.rectangle([0, 0, t.width - 1, t.height - 1], outline=STRIP_OK if ok else STRIP_BAD, width=width)
        tiles.append(t)
    w, h = tiles[0].size
    rows = (len(tiles) + STRIP_COLS - 1) // STRIP_COLS
    total_w = STRIP_COLS * (w + STRIP_GAP) + STRIP_GAP
    out = Image.new("RGB", (total_w, STRIP_HEADER_H + rows * (h + STRIP_GAP) + STRIP_GAP), (255, 255, 255))
    head = ImageDraw.Draw(out)
    head.rectangle([0, 0, total_w - 1, STRIP_HEADER_H - 1], fill=STRIP_OK if all_ok else STRIP_BAD)
    head.text((14, 22), ("PASS" if all_ok else "FAIL") + "  colour", fill=(255, 255, 255))
    for i, t in enumerate(tiles):
        r, c = divmod(i, STRIP_COLS)
        out.paste(t, (STRIP_GAP + c * (w + STRIP_GAP), STRIP_HEADER_H + STRIP_GAP + r * (h + STRIP_GAP)))
    buf = io.BytesIO()
    out.save(buf, "PNG")
    write_atomic(path, buf.getvalue())


class Blobs:
    """The `>|blob name= len= crc=` records and their `>|d=` lines, checked
    by length and CRC. `good` maps a name to its bytes; `bad` lists the
    names that arrived short or wrong."""

    def __init__(self):
        self.head, self.buf = None, []
        self.good, self.bad = {}, []

    def feed(self, rec):
        """Take a record; True when it was part of a blob."""
        if rec.startswith("d=") and self.head is not None:
            self.buf.append(rec[2:])
            return True
        self.flush()
        if rec.startswith("blob "):
            self.head = fields(rec)
            return True
        return False

    def flush(self):
        if self.head is None:
            return
        head, text = self.head, "".join(self.buf)
        self.head, self.buf = None, []
        try:
            data = base64.b64decode(text, validate=True)
        except ValueError:
            data = b""
        name = head.get("name", "")
        try:
            whole = (len(data) > 0 and len(data) == int(head["len"])           # an empty blob is never a picture
                     and (zlib.crc32(data) & 0xFFFFFFFF) == int(head["crc"], 0))   # decimal or 0x hex
        except (KeyError, ValueError):
            whole = False
        if whole:
            self.good[name] = data
            if name in self.bad:
                self.bad.remove(name)
        elif name not in self.bad:
            self.bad.append(name)


class Link:
    """kcli's Board, with the records and the `!` lines of each answer."""

    def __init__(self, port):
        self.board = kcli.Board(port)

    def close(self):
        self.board.close()

    def read_line(self, deadline):
        return self.board.read_line(deadline)

    def exchange(self, line, timeout=10.0, on_line=None):
        """Send `line`; return (records, `!` lines) at `>ok`, raise
        CommandError at `>err` or when nothing ends it in time."""
        self.board.send_line(line)
        deadline = CLOCK.monotonic() + timeout
        recs, logs = [], []
        while True:
            x = self.board.read_line(deadline)
            if x is None:
                raise CommandError(line, None, recs, logs)
            if x.startswith(">|"):
                recs.append(x[2:])
            elif x.startswith(">ok"):
                return recs, logs
            elif x.startswith(">err"):
                raise CommandError(line, x, recs, logs)
            elif x.startswith("!"):
                logs.append(x)
            else:
                continue
            if on_line:
                on_line(x)

    def cmd(self, line, timeout=10.0):
        return self.exchange(line, timeout)[0]


class Station:
    """One run of the station on one Kode."""

    def __init__(self, a):
        self.a = a
        self.events = a.events
        self.out = Path(a.out).expanduser()
        self.config_path = Path(a.config).expanduser()
        self.cfg = load_config(self.config_path)
        self.link = None
        self.info = None
        self.outdir = None
        self.results = []
        self.blocks = {}
        self.t0 = CLOCK.monotonic()
        # A bench run never resets or locks a Kode: an injected operator, an
        # NFC tag left unwritten or a placement skipped is not a factory test.
        self.bench = bool(a.operator_sim or a.no_write or a.no_place)
        self.rng = random.Random(OPERATOR_SIM_SEED)
        self.sim_queue = []                  # [(due, line, key)] the bench operator's presses
        self.sim_missed = set()
        self.pending = {}                    # rows finished later by the PC: nfc and rfid
        self.done_bg = set()
        self.extra = {}
        self.exit = dict(values={}, reasons={})
        self.photos = {}
        self.repeat_of = None
        self.run_ok = False                  # the run reached its end on the Kode
        self.interrupted = False             # SIGTERM or Ctrl-C: the page's Stop
        self.lock = LOCK_NOT_SENT
        self.op_end_t = None

    # ---- events and steps ---------------------------------------------------
    def emit(self, **ev):
        if self.events:
            print(EVENT_PREFIX + " " + json.dumps(ev, ensure_ascii=False, separators=(",", ":")), flush=True)

    def log(self, text, level="info"):
        self.emit(type="log", text=text, level=level, t=CLOCK.time())
        if not self.events:
            print(text, flush=True)

    def phase(self, name, text=None, **kw):
        self.emit(type="phase", name=name, text=text or PHASE_TEXT.get(name, name), t=CLOCK.time(), **kw)

    def begin(self, sid):
        self.emit(type="step", id=sid, state="running", t=CLOCK.time())

    def has(self, sid):
        return any(r["id"] == sid for r in self.results)

    def step(self, sid):
        return next((r for r in self.results if r["id"] == sid), None)

    def add(self, sid, status, measured="", rng="", secs=0.0, images=(), detail="", values=None, name=None):
        name = name or STEPS[sid][0]
        self.results.append(dict(n=len(self.results) + 1, id=sid, name=name, status=status, measured=measured,
                                 range=rng, secs=round(secs, 2), images=list(images), detail=detail,
                                 values=values if values is not None else values_from(measured)))
        print(f"{len(self.results):2d} {name:36s} {status:11s} {secs:6.1f}s  {measured[:90]}", flush=True)
        img = str(self.outdir / images[0]) if images and self.outdir else None
        self.emit(type="result", id=sid, state=EVENT_STATE.get(status, status), values=self.results[-1]["values"],
                  image=img, reason=reason_from(status, measured, detail), step_s=round(secs, 2),
                  total_s=round(CLOCK.monotonic() - self.t0, 1))

    def tick(self, block, t0):
        self.blocks[block] = round(self.blocks.get(block, 0.0) + CLOCK.monotonic() - t0, 2)

    # ---- the session -------------------------------------------------------
    def run(self):
        a = self.a
        if a.discover:
            return self.discover()
        stop_why = "not run"
        try:
            try:
                signal.signal(signal.SIGTERM, _sigterm)
            except ValueError:          # not the main thread (a test harness)
                pass
            refused = self.connect()
            if refused:
                return refused
            if not a.no_place and not self.place():
                raise Abort("the placement did not complete")
            self.run_all()
            self.finish_pending()
            self.exit_record()
        except Abort as e:
            stop_why = f"not run: {e}"
            print(stop_why, flush=True)
        except KeyboardInterrupt:
            self.log("interrupted: the port is let go")
            stop_why = "the test was interrupted before this step"
            self.interrupted = True
        except Exception as e:
            self.log(f"the test stopped: {type(e).__name__}: {e}", "error")
            print(traceback.format_exc(), flush=True)
            stop_why = f"the test stopped before this step: {type(e).__name__}: {e}"
        if self.info is None:
            self.close()
            return EXIT_NO_REPORT
        return self.conclude(stop_why)

    def connect(self):
        """USB, the port, ping, `info` and the station verbs. A non-zero
        return ends the program with no report (nothing is known of the Kode,
        or its kodeOS cannot run the station)."""
        a, cfg = self.a, self.cfg
        if not port_present(a.port):
            usb_s = USB_WAIT_OVERRIDE_S if USB_WAIT_OVERRIDE_S is not None else cfg["place"]["usb_s"]
            if not self.wait_usb(a.port, usb_s):
                self.info = dict(serial="-", mac="-", kodeos="-", c5="-", date=self.now_text())
                self.make_outdir("no_kode")
                self.emit_start()
                self.begin("place")
                self.add("place", NOVERDICT, "Kode not detected on USB", f"appears within {usb_s} s", values=[])
                self.emit(type="place", state="no_usb", text="Kode not detected on USB", t=CLOCK.time())
                raise Abort("Kode not detected on USB")
        holders = port_holders(a.port)
        if holders:
            self.log(f"{a.port} is held by {', '.join(map(str, holders))}: close it and start again", "error")
            return EXIT_NO_REPORT
        t0 = CLOCK.monotonic()
        self.link = Link(a.port)
        for _ in range(PING_TRIES):
            try:
                self.link.cmd("ping", PING_TIMEOUT_S)
                break
            except CommandError:
                continue
        else:
            self.log("the Kode does not answer on the console", "error")
            return EXIT_NO_REPORT
        self.link.board.drain(0.3)
        self.tick("start", t0)
        recs = self.link.cmd("info")
        self.info = self.read_info(recs)
        try:
            self.link.cmd("test_dot_info")
        except CommandError as e:
            if e.msg == "locked":
                why = "this Kode is locked: it already passed the station"
            elif e.code == 1:
                why = (f"this kodeOS ({self.info['kodeos']}) has no test_dot verbs: install a kodeOS built "
                       f"with the station option")
            else:
                why = f"test_dot_info refused: {e.term or 'no answer'}"
            self.info = None
            self.log(why, "error")
            self.close()
            return EXIT_NO_REPORT
        self.screen("connecting")
        self.make_outdir(self.info["serial"])
        self.find_repeat()
        self.emit_start()
        self.begin("info")
        self.add("info", PASS if self.info["serial"] != "-" else FAIL,
                 f"serial {self.info['serial']}; MAC {self.info['mac']}; kodeOS {self.info['kodeos']}; C5 {self.info['c5']}",
                 detail="\n".join(recs),
                 values=[value("serial", self.info["serial"]), value("kodeos", self.info["kodeos"]),
                         value("c5", self.info["c5"])])
        return 0

    def read_info(self, recs):
        kv = {}
        for r in recs:
            f = fields(r)
            if "serial" in f and "serial" not in kv:
                kv["serial"] = f["serial"]
            if "mac_p4" in f:
                kv["mac"] = f["mac_p4"]
        running = next((fields(r) for r in recs if r.startswith("slot=") and "state=running" in r), {})
        c5 = next((fields(r) for r in recs if r.startswith("c5=")), {})
        return dict(serial=kv.get("serial", "-"), mac=kv.get("mac", "-"), kodeos=running.get("version", "-"),
                    c5=c5.get("version", "-"), date=self.now_text())

    def now_text(self):
        return datetime.fromtimestamp(CLOCK.time()).strftime("%Y-%m-%d %H:%M:%S")

    def make_outdir(self, who):
        stamp = datetime.fromtimestamp(CLOCK.time()).strftime("%Y%m%d-%H%M%S")
        self.outdir = self.out / f"{stamp}_{who}"
        self.outdir.mkdir(parents=True, exist_ok=True)

    def find_repeat(self):
        """The same Kode back after no PASS, within the window, without being
        taken out: this run repeats the earlier one, and links it."""
        try:
            last = json.loads((self.out / "last_report.json").read_text())
        except (OSError, ValueError):
            return
        if (last.get("serial") == self.info["serial"] and last.get("verdict") != PASS
                and CLOCK.time() - last.get("t", 0) < REPEAT_WINDOW_S and last.get("folder") != self.outdir.name):
            self.repeat_of = dict(folder=last.get("folder"), verdict=last.get("verdict"), report=last.get("report"))

    def emit_start(self):
        extra = {"repeat_of": self.repeat_of} if self.repeat_of else {}
        self.emit(type="start", t=CLOCK.time(), serial=self.info["serial"],
                  versions=dict(kodeos=self.info["kodeos"], c5=self.info["c5"], mac=self.info["mac"]),
                  steps=[dict(id=i, name=STEPS[i][0], block=STEPS[i][1]) for i in self.planned()], **extra)

    def planned(self):
        return [i for i in ORDER if not (i == "place" and self.a.no_place)]

    def wait_usb(self, port, seconds):
        t = CLOCK.monotonic()
        self.emit(type="place", state="usb", text="Waiting for the Kode on USB", t=CLOCK.time())
        while not port_present(port):
            if CLOCK.monotonic() - t >= seconds:
                return False
            CLOCK.sleep(0.5)
        return True

    def screen(self, phase, verdict=None):
        """Ask the Kode for one of the two screens the PC draws: never stops the test."""
        line = f"test_dot_screen phase={phase}" + (f" verdict={verdict}" if verdict else "")
        try:
            self.link.cmd(line)
        except CommandError as e:
            self.log(f"{line}: {e.term or 'no answer'}")

    # ---- placement ---------------------------------------------------------
    def place(self):
        """The Kode in the fixture: the camera on J3, the four marks, the
        offset and the turn inside their limits. True: the test starts."""
        c = self.cfg["place"]
        self.begin("place")
        t1 = CLOCK.monotonic()
        self.emit(type="place", state="waiting", text="Put the Kode in the nest", t=CLOCK.time())
        self.phase("place")
        win = c.get("window") or PLACE_DEFAULTS["window"]
        line = (f"test_dot_place win={quote(f'{int(win[0])},{int(win[1])}')} "
                f"cam_ms={int(float(c['camera_s']) * 1000)} mark_ms={int(float(c['marks_s']) * 1000)} "
                f"dx_mm={num_text(c['dx_mm'])} deg={num_text(c['turn_deg'])}")
        blobs, recs, res = Blobs(), [], None

        def on_line(x):
            nonlocal res
            if not x.startswith(">|"):
                return
            r = x[2:]
            if blobs.feed(r):
                return
            recs.append(r)
            f = fields(r)
            if r.startswith("place ") and "state" in f:
                st = f["state"]
                self.emit(type="place", state=st, text=PLACE_TEXT.get(st, st), t=CLOCK.time())
            elif r.startswith("place ") and "result" in f:
                res = f
        term = ">ok"
        try:
            self.link.exchange(line, float(c["camera_s"]) + float(c["marks_s"]) + 70, on_line)
        except CommandError as e:
            term = e.term
        blobs.flush()
        img = self.save_cam_blobs(blobs)
        img = [img["location"]] if "location" in img else []
        dt = CLOCK.monotonic() - t1
        r = (res or {}).get("result")
        self.blocks["placement"] = round(dt, 2)
        if r == "ok":
            msg = (f"placed: offset {res.get('dx_mm')} / {res.get('dy_mm')} mm, turn {res.get('ang_deg')} degrees, "
                   f"{res.get('tries')} checks")
            self.add("place", PASS, msg, f"offset <= {c['dx_mm']} mm, turn <= {c['turn_deg']} degrees", dt, img,
                     values=values_from(" ".join(recs[-1:])))
            self.emit(type="place", state="ok", text="Kode placed: the test starts", t=CLOCK.time())
            self.t0 = CLOCK.monotonic()          # the run's clock starts here
            w = res.get("win")
            if w:                                # the last good window: looked at first next time
                try:
                    self.cfg["place"]["window"] = [int(v) for v in w.split(",")]
                    save_config(self.config_path, self.cfg)
                except (ValueError, OSError):
                    pass
            return True
        detail = "\n".join(recs + [term or "no answer"])
        if r == "cam_missing":
            msg = "Top connector (J3): the camera does not answer (camera module, its cable or the connector)"
            self.add("place", FAIL, msg, f"OV5640 camera on J3 within {c['camera_s']} s", dt, img, detail)
            self.emit(type="place", state="j3_failed", text=msg, reason="top_connector_j3", t=CLOCK.time())
        elif r == "panel_dead":
            msg = "Screen does not answer: the panel confirms no frame (not even after recovering it)"
            self.add("place", FAIL, msg, "the panel confirms a frame", dt, img, detail)
            self.emit(type="place", state="panel_failed", text=msg, reason="screen_not_answering", t=CLOCK.time())
        elif r == "misplaced":
            self.add("place", NOVERDICT,
                     f"Kode badly seated: marks seen {res.get('marks')}, offset {res.get('dx_mm')} / {res.get('dy_mm')} mm, "
                     f"turn {res.get('ang_deg')} degrees (limits {res.get('limit_mm')} mm and {res.get('limit_deg')} degrees)",
                     "the 4 marks, inside the limits", dt, img, detail)
            self.emit(type="place", state="badly_seated", text="Kode badly seated",
                      image=str(self.outdir / img[0]) if img else None, t=CLOCK.time())
        else:
            self.add("place", NOVERDICT, "The Kode did not answer the placement", "", dt, img, detail)
        return False

    def save_cam_blobs(self, blobs):
        """The camera's pictures as `img_cam_<name>.jpg`; {name: file}."""
        out = {}
        for name, data in blobs.good.items():
            if not re.fullmatch(r"[A-Za-z0-9_]+", name or ""):
                continue
            fn = f"img_cam_{name}.jpg"
            write_atomic(self.outdir / fn, data)
            out[name] = fn
            self.emit(type="camera", image=str(self.outdir / fn), pattern=name, t=CLOCK.time())
        return out

    # ---- the run -----------------------------------------------------------
    def run_all(self):
        """`test component=all`: every row, the camera and its LED and the
        operator, each reported as its record arrives."""
        t_all = CLOCK.monotonic()
        deadline = t_all + RUN_BUDGET_S
        self.phase("camera", limit_s=CAMERA_EXPECTED_S)
        s = types.SimpleNamespace(term=None, abort_why="", cam=Blobs(), cam_tests={}, cam_detail=[], timing={},
                                  cam_timing={}, logs=[], op_steps={"buttons": [], "tp": []}, cur_op="buttons",
                                  begun=set(), cam_done=False, polling=False, lane_idle=False, poll_done=False,
                                  buttons_phase=False)
        self.state = s

        def start(i):
            if i not in s.begun:
                s.begun.add(i)
                self.begin(i)

        def next_row():
            for q in ROW_SEQ:
                if q not in s.begun:
                    start(q)
                    return

        start("sd")

        def handle(x):
            """One line of the run (from the open line or from a poll)."""
            if x.startswith("!"):
                s.logs = (s.logs + [x])[-40:]
                return
            if not x.startswith(">|"):
                return
            r = x[2:]
            if s.cam.feed(r):
                return
            f = fields(r)
            if r.startswith("poll "):
                if "dropped" in f:
                    self.log(f"the Kode's poll ring dropped {f['dropped']} records", "error")
                if "state" in f:
                    s.poll_done = f["state"] == "done"
                    s.lane_idle = f.get("lane") == "idle"
                return
            if r.startswith("step="):
                s.op_steps[s.cur_op].append(r)
                self.operator_event(f)
                self.operator_sim(f)
                return
            if r.startswith("timing="):
                if f["timing"] == "all":
                    s.timing = f
                else:
                    s.cam_timing = f
                return
            if r.startswith("colour_info"):
                s.cam_detail.append(r)
                return
            name = f.get("test")
            if name is None:
                return
            if name in CAM_IDS or name == "led":
                s.cam_tests[name] = f
                s.cam_detail.append(r)
                return
            if name == "all":
                s.logs.append("test all: " + r)
                if f.get("result") in ("aborted", "timeout"):
                    why = f.get("why", "?")
                    s.abort_why = "the run stopped early: " + (
                        "microSD not detected (seat it and repeat)" if why == "sd_missing" else why)
                return
            if name in ("buttons", "tp"):
                st = ROW_WORD.get(f.get("result", ""), TIMEOUT)
                steps = s.op_steps[name]
                start(name)
                self.add(name, st, r + ("  |  " + "  |  ".join(steps) if steps else ""), "",
                         float(f.get("ms", 0)) / 1000, detail="\n".join(steps + [r])[-REPORT_DETAIL_CHARS:])
                if name == "buttons":
                    s.cur_op = "tp"
                    start("tp")
                    self.phase("touch")
                else:
                    self.op_end_t = CLOCK.monotonic()
                    self.phase("rest")
                return
            if name in ROW_SEQ:
                st = ROW_WORD.get(f.get("result", ""), TIMEOUT)
                ms = float(f.get("ms", 0)) / 1000
                det = "\n".join(s.logs + [r])[-REPORT_DETAIL_CHARS:]
                s.logs = []
                nums = " ".join(re.findall(r"(?:min|max|low|high|lo|hi)=\S+", det))
                start(name)
                if name in ("nfc", "rfid"):
                    self.pending[name] = (st, r, ms, det)      # written and read back by the PC later
                else:
                    note = "  |  microSD not detected or not answering" if name == "sd" and st != PASS else ""
                    self.add(name, st, r + note, nums, ms, detail=det)
                self.blocks["kodeos_rows"] = round(self.blocks.get("kodeos_rows", 0.0) + ms, 2)
                next_row()

        self.handle = handle
        self.link.board.send_line("test component=all")
        while True:
            x = self.link.read_line(deadline)
            if x is None:
                break
            if x.startswith(">ok") or x.startswith(">err"):
                s.term = x
                break
            handle(x)
        s.cam.flush()
        if s.term and s.term.startswith(">ok"):
            self.camera_done()
            s.polling = True
            self.poll_loop(deadline)
        if s.cam.bad:
            self.retry_blobs()
        self.blocks["run"] = round(CLOCK.monotonic() - t_all, 2)
        if s.timing:
            self.blocks["run/kode_total"] = round(float(s.timing.get("total_ms", 0)) / 1000, 2)
            self.blocks["run/camera_end"] = round(float(s.timing.get("cam_end_ms", 0)) / 1000, 2)
        self.run_ok = s.poll_done
        # What the run did not give back is the run's failure, not a step "not run".
        why = s.abort_why or ("the run did not return this step" + (
            "" if self.run_ok else f" (end: {s.term or 'no answer'})"))
        for sid in RUN_IDS:
            if not self.has(sid) and sid not in self.pending:
                self.begin(sid)
                self.add(sid, TIMEOUT, why, values=[], detail="\n".join(s.logs)[-REPORT_DETAIL_CHARS:])

    def camera_done(self):
        """The camera's tests and its LED, at the run line's end."""
        s = self.state
        tm = s.cam_timing
        for k, b in (("photos_ms", "camera/photos"), ("analysis_ms", "camera/analysis"), ("led_ms", "camera/led")):
            if k in tm:
                self.blocks[b] = round(float(tm[k]) / 1000, 2)
        imgs = self.save_cam_blobs(s.cam)
        loc = imgs.get("location")
        for i in CAM_IDS:
            if i not in s.begun:
                s.begun.add(i)
                self.begin(i)
            f = s.cam_tests.get(i)
            if f is None:
                self.add(i, NOVERDICT, "the Kode did not return this test", images=[loc] if loc else [], values=[])
                continue
            st = CAM_WORD.get(f.get("result", ""), NOVERDICT)
            meas = " ".join(f"{k}={v}" for k, v in f.items() if k not in ("test", "result"))
            meas = meas.replace("why=reflection_or_light",
                                "reason: a reflection or light on the glass (cover the light or move the station)")
            vals, rng = [], []
            for k, v in f.items():
                if k in ("test", "result", "why", "at"):
                    continue
                try:
                    val = float(v) if "." in v else int(v)
                except ValueError:
                    val = v
                if k.startswith(("min_", "max_")) or k in ("tol", "limit_ratio"):
                    rng.append(f"{k}={v}")
                unit = "mm" if k.endswith("_mm") else ("degrees" if k.endswith("_deg") else "")
                vals.append(value(k, val, unit))
            pics = []
            if i == "cam_pixels":
                pics = [b for k, b in imgs.items() if k.startswith(("dead_", "on_"))]
            elif i == "cam_zones":
                pics = [b for k, b in imgs.items() if k.startswith("zone_")]
            elif i == "cam_colour" and "colours" in s.cam.good:
                try:
                    colour_strip(s.cam.good["colours"], f, self.outdir / "img_cam_colour_strip.png")
                    pics = ["img_cam_colour_strip.png"]
                except Exception as e:           # a picture never decides a verdict
                    self.log(f"colour strip: {type(e).__name__}: {e}")
            if not pics and loc:
                pics = [loc]
            self.add(i, st, meas, " ".join(rng), 0.0, pics,
                     detail="\n".join(s.cam_detail)[-REPORT_DETAIL_CHARS:], values=vals)
        s.begun.add("led")
        self.begin("led")
        lf = s.cam_tests.get("led")
        if lf is None:
            self.add("led", NOVERDICT, "the Kode did not return the LED", values=[])
        else:
            meas = " ".join(f"{k}={v}" for k, v in lf.items() if k not in ("test", "result"))
            self.add("led", {"pass": PASS, "fail": FAIL}.get(lf.get("result"), NOVERDICT), meas,
                     f"flux > {lf.get('min_flux', '?')}; distance < {lf.get('max_dist', '?')}",
                     float(tm.get("led_ms", 0)) / 1000)
        s.cam_done = True
        if not s.buttons_phase:
            s.buttons_phase = True
            s.begun.add("buttons")
            self.begin("buttons")
            self.phase("buttons")

    def retry_blobs(self):
        """A camera picture that arrived broken is asked for again, three
        times at most; only the two the Kode keeps by name can be."""
        s = self.state
        for name in list(s.cam.bad):
            if name not in ("location", "colours"):
                self.log(f"picture {name} arrived broken and cannot be asked again")
                continue
            for _ in range(BLOB_TRIES):
                b = Blobs()
                try:
                    for r in self.link.cmd(f"test_dot_blob name={quote(name)}", 30.0):
                        b.feed(r)
                    b.flush()
                except CommandError as e:
                    self.log(f"test_dot_blob {name}: {e.term or 'no answer'}")
                    continue
                if name in b.good:
                    s.cam.good[name] = b.good[name]
                    s.cam.bad.remove(name)
                    self.save_cam_blobs(b)
                    break
            self.log(f"picture {name}: " + ("recovered" if name in s.cam.good else "not whole after 3 tries"))

    def poll_loop(self, deadline):
        """The rest of the run through `test_dot_poll`, and the PC's own work
        beside the operator whenever the Kode's lane is free."""
        s = self.state
        want = self.cfg.get("parallel_with_operator")
        want = want if isinstance(want, list) else BACKGROUND
        todo = [w for w in BACKGROUND if w in want]
        while CLOCK.monotonic() < deadline:
            self.pump()
            if s.poll_done and not s.lane_idle:
                s.lane_idle = True               # the run is over: the line is free
            ready = [w for w in todo if w not in ("nfc", "rfid") or w in self.pending]
            if s.poll_done and not ready:
                break
            if ready and s.cam_done and s.lane_idle:
                todo.remove(ready[0])
                self.background(ready[0])
                continue
            CLOCK.sleep(POLL_PERIOD_S)
        else:
            self.log("the run went over its budget")

    def pump(self):
        """One `test_dot_poll`: the records the Kode kept, by the same path as the open line."""
        self.operator_sim_pump()
        try:
            recs = self.link.cmd("test_dot_poll", POLL_TIMEOUT_S)
        except CommandError as e:
            self.state.logs.append(f"test_dot_poll: {e.term or 'no answer'}")
            return
        for r in recs:
            self.handle(">|" + r)

    def sleep_pump(self, seconds):
        """Wait while still polling, so the operator's steps keep arriving."""
        end = CLOCK.monotonic() + seconds
        while True:
            left = end - CLOCK.monotonic()
            if left <= 0:
                return
            CLOCK.sleep(min(0.25, left))
            self.pump()

    def background(self, what):
        t = CLOCK.monotonic()
        self.done_bg.add(what)
        try:
            if what == "radio":
                self.radio_block()
            elif what == "nfc":
                self.nfc_step()
            elif what == "rfid":
                self.rfid_step()
            elif what == "pads":
                self.pads_block(self.sleep_pump)
        except Exception as e:
            self.log(f"beside the operator, {what}: {type(e).__name__}: {e}", "error")
        self.blocks["parallel/" + what] = round(CLOCK.monotonic() - t, 2)

    def finish_pending(self):
        """What the run left for the PC and the operator's time did not cover."""
        if "rfid" in self.pending and "rfid" not in self.done_bg:
            self.rfid_step()
        if "nfc" in self.pending and "nfc" not in self.done_bg:
            self.nfc_step()
        if "radio" not in self.done_bg:
            try:
                self.radio_block()
            except Exception as e:
                for i in RADIO_IDS:
                    if not self.has(i):
                        self.add(i, NOVERDICT, f"{type(e).__name__}: {e}", detail=traceback.format_exc(), values=[])
        if "pads" not in self.done_bg:
            self.pads_block(CLOCK.sleep)

    # ---- the operator --------------------------------------------------------
    def operator_event(self, f):
        """A button or touch step, to the page as it happens."""
        if "step" not in f:
            return
        k = int(f["step"])
        if "of" in f and ("press" in f or "target" in f):
            kind = "press" if "press" in f else "target"
            if kind == "press" and not self.state.buttons_phase:
                self.state.buttons_phase = True
                self.phase("buttons")
            self.emit(type="operator", step=k, of=int(f["of"]), kind=kind, what=f.get(kind),
                      limit_s=OPERATOR_LIMIT_S, t=CLOCK.time())
            return
        if "result" not in f:
            return
        r = f["result"]
        ms = int(f["ms"]) if f.get("ms", "").isdigit() else None
        common = dict(type="operator", step=k, kind="answer", button=f.get("button"),
                      pressed=f.get("pressed") or None, ms=ms)
        if r in OPERATOR_REPEAT:
            self.emit(result="repeat", reason=OPERATOR_REPEAT[r], limit_s=OPERATOR_LIMIT_S, t=CLOCK.time(), **common)
        elif r in OPERATOR_FAIL:
            self.emit(result="fail", reason=OPERATOR_FAIL[r], **common)
        else:
            self.emit(result=r, target=f.get("target"), got=f.get("got"), **common)

    def operator_sim(self, f):
        """The bench's operator: a prompt from the Kode is answered the way a
        person would, 0.6 to 1.5 s later, now and then with a wrong button
        first or one touch off the target (the Kode forgives both and asks
        again). Every press goes down the console's injection verbs."""
        if not self.a.operator_sim or "of" not in f:
            return
        key = (f.get("step"), f.get("press") or f.get("target"))
        if any(q[2] == key for q in self.sim_queue):
            return                                     # a prompt repeated while its answer is still queued
        now, rng = CLOCK.monotonic(), self.rng
        d = rng.uniform(0.6, 1.5)
        q = []
        if f.get("press") in INJECT_ID:
            want = INJECT_ID[f["press"]]
            if rng.random() < 0.12:
                wrong = rng.choice([v for v in INJECT_ID.values() if v != want])
                q += [(now + d, f"btn id={wrong} state=press"), (now + d + 0.3, f"btn id={wrong} state=release")]
                d += 0.3 + rng.uniform(0.6, 1.5)
            q += [(now + d, f"btn id={want} state=press"), (now + d + 0.3, f"btn id={want} state=release")]
        elif "target" in f:
            tx, ty = (int(v) for v in f["target"].split(","))

            def glass(px, py):                         # the console takes points on the glass only
                return (min(max(px, 0), GLASS_W_PX - 1), min(max(py, 0), GLASS_H_PX - 1))
            if rng.random() < 0.12 and key not in self.sim_missed:
                self.sim_missed.add(key)
                mx, my = glass(tx + SIM_MISS_PX, ty + SIM_MISS_PX)
                q += [(now + d, f"touch x={mx} y={my} state=down"),
                      (now + d + 0.2, f"touch x={mx} y={my} state=up")]
                d += 0.2 + rng.uniform(0.6, 1.5)
            x, y = glass(tx + rng.randint(-SIM_JITTER_PX, SIM_JITTER_PX), ty + rng.randint(-SIM_JITTER_PX, SIM_JITTER_PX))
            q += [(now + d, f"touch x={x} y={y} state=down"), (now + d + 0.2, f"touch x={x} y={y} state=up")]
        self.sim_queue += [(t, line, key) for t, line in q]
        self.sim_queue.sort(key=lambda e: e[0])

    def operator_sim_pump(self):
        now = CLOCK.monotonic()
        while self.sim_queue and self.sim_queue[0][0] <= now:
            _, line, _ = self.sim_queue.pop(0)
            try:
                self.link.cmd(line, 3.0)
            except CommandError as e:
                self.log(f"bench operator: {e}")
            now = CLOCK.monotonic()

    # ---- NFC and RFID ------------------------------------------------------
    def rfid_step(self):
        """RFID 125 kHz: the ID from the Kode's MAC written into the station's
        T5577 and read back; PASS only when what is read is what was written."""
        self.begin("rfid")
        t0 = CLOCK.monotonic()
        st, row, _, det0 = self.pending.pop("rfid", (TIMEOUT, "no row", 0, ""))
        det = [det0]
        write = not self.a.no_write
        want = rfid_id_of(self.info["mac"])
        tag, tw, tr, wrote = None, None, None, False
        if write and want:
            t1 = CLOCK.monotonic()
            try:
                wr, _ = self.link.exchange(f"rfid_write id={quote(want)}", 60.0)
                det.append(" ".join(wr))
                wrote = any(fields(r).get("match") == "yes" for r in wr)
            except CommandError as e:
                det.append(str(e))
            tw = CLOCK.monotonic() - t1
        for _ in range(3):
            t2 = CLOCK.monotonic()
            try:
                recs = self.link.cmd("rfid_read ms=3000", 15.0)
            except CommandError as e:
                det.append(str(e))
                continue
            det.append(" ".join(recs))
            m = next((fields(r) for r in recs if r.startswith("rfid ")), None)
            if m and m.get("id"):
                tag, tr = m, CLOCK.monotonic() - t2
                break
        read = (tag or {}).get("id", "").upper()
        if write and want:
            info = f"written {want}; read {read or 'nothing'}"
            if tw is not None and tr is not None:
                info += f"; write {tw:.1f} s (checked by the Kode: {'yes' if wrote else 'no'}), read {tr:.1f} s"
            valid = bool(tag) and read == want
        else:
            info = f"EM4100 at RF/{tag.get('rf', '?')}; ID {tag['id']}" if tag else ""
            valid = bool(tag) and re.fullmatch(r"([0-9A-Fa-f]{2}:){4}[0-9A-Fa-f]{2}", tag["id"]) is not None
        status = PASS if valid else (TIMEOUT if st == TIMEOUT and not tag else FAIL)
        vals = [value("protocol", "EM4100"), value("id_written", want or ""), value("id_read", read),
                value("t_write", round(tw, 2) if tw is not None else 0, "s"),
                value("t_read", round(tr, 2) if tr is not None else 0, "s")]
        self.add("rfid", status, (info or "no ID") + f"; kodeOS row test=rfid: {st}",
                 "read = written (a 5-byte ID from the Kode's MAC)", CLOCK.monotonic() - t0,
                 detail="\n".join(det)[-REPORT_DETAIL_CHARS:], values=vals)

    def nfc_step(self):
        """NFC: an NDEF text with the Kode's serial written into the station's
        NTAG and read back; PASS only when it matches."""
        self.begin("nfc")
        t0 = CLOCK.monotonic()
        st, row, _, det0 = self.pending.pop("nfc", (TIMEOUT, "no row", 0, ""))
        det = [det0]
        serial = self.info["serial"]
        write = not self.a.no_write
        tw = tr = None
        read, uid, rd = "", "", []
        skip_read = False
        try:
            if write:
                t1 = CLOCK.monotonic()
                try:
                    wr, _ = self.link.exchange(f"nfc_write text={quote(serial)}", 30.0)
                    det.append(" ".join(wr))
                except CommandError as e:
                    wr = e.records
                    det.append(" ".join(wr) + f" {e.term}")
                    skip_read = e.term is not None and "no tag" in e.term   # nothing to read back
                tw = CLOCK.monotonic() - t1
                uid = next((fields(r).get("uid", "") for r in wr if r.startswith("nfc=tag")), "")
            if skip_read:
                det.append("nfc_read skipped: the write found no tag")
            else:
                t2 = CLOCK.monotonic()
                try:
                    rd = self.link.cmd("nfc_read ms=3000", 30.0)
                except CommandError as e:
                    rd = e.records
                    det.append(str(e))
                tr = CLOCK.monotonic() - t2
                det.append(" ".join(rd))
            for r in rd:
                f = fields(r)
                if r.startswith("nfc=ndef"):
                    read = f.get("text", "")
                if r.startswith("nfc=tag") and not uid:
                    uid = f.get("uid", "")
        except Exception as e:
            det.append(f"{type(e).__name__}: {e}")
        ok = (read == serial) if write else bool(uid)
        info = (f"written {serial}; " if write else "") + f"read {read or 'nothing'}" + (f"; tag {uid}" if uid else "")
        if tw is not None and tr is not None:
            info += f"; write {tw:.1f} s, read {tr:.1f} s"
        if skip_read:
            info += "; the write found no tag"     # the station's tag, not the Kode (ui/reports.py)
        status = PASS if (ok and st == PASS) else (TIMEOUT if st == TIMEOUT and not read else FAIL)
        vals = [value("text_written", serial if write else ""), value("text_read", read), value("uid", uid),
                value("t_write", round(tw, 2) if tw is not None else 0, "s"),
                value("t_read", round(tr, 2) if tr is not None else 0, "s")]
        self.add("nfc", status, info + f"; kodeOS row test=nfc: {st}",
                 "read = written (an NDEF text with the Kode's serial)", CLOCK.monotonic() - t0,
                 detail="\n".join(det)[-REPORT_DETAIL_CHARS:], values=vals)

    # ---- the rear pads -------------------------------------------------------
    def pads_block(self, sleep):
        """The rear pads through the station's relays and ADC, which the Kode
        drives. Whatever happens, at the end: boost off, charging, rest."""
        t_blk = CLOCK.monotonic()
        pads = Pads(self.link, sleep)
        done = set()

        def fin(sid, status, measured, rng, t0, vals, detail=""):
            self.add(sid, status, measured, rng, CLOCK.monotonic() - t0, detail=detail, values=vals)
            done.add(sid)

        try:
            t0 = CLOCK.monotonic()
            try:                                           # 0) the station answers
                pads.relays(RELAY_REST)
                pads.adc(3)
            except (PadsError, CommandError) as e:
                for sid in PADS_IDS:
                    self.begin(sid)
                    fin(sid, NOVERDICT, STATION_FAULT + "the relays or the ADC do not answer", "", t0, [],
                        detail=f"{type(e).__name__}: {e}")
                return
            t0 = CLOCK.monotonic()                         # 1) at rest
            a = {k: pads.adc(k) for k in (0, 1, 2, 3)}
            rest = [value(f"rest_a{k}_v", a[k], "V") for k in (0, 1, 2, 3)]
            if a[3] < PC_5V_MIN_V:
                for sid in PADS_IDS:
                    self.begin(sid)
                    fin(sid, NOVERDICT, STATION_FAULT + "no 5 V from the PC", f"A3 >= {PC_5V_MIN_V:.1f} V", t0, rest,
                        detail=f"A3={a[3]} V")
                return
            pogo5 = a[0] >= POGO_5V_MIN_V
            self.begin("pads_gnd")                         # 2) the GND pad
            t0 = CLOCK.monotonic()
            pads.relays(RELAY_GND)
            sleep(GND_SETTLE_S)
            g = pads.adc(1)
            pads.relays(RELAY_REST)
            if g < GND_PAD_MAX_V:
                s_, m_ = PASS, f"A1={g} V with the pad grounded"
            elif g >= GND_OPEN_MIN_V:
                s_, m_ = FAIL, f"GND pogo pin without contact (A1={g} V)"
            else:
                s_, m_ = FAIL, f"GND pad doubtful (A1={g} V)"
            gnd_bad = g if s_ != PASS else None
            fin("pads_gnd", s_, m_, f"A1 < {GND_PAD_MAX_V:.1f} V", t0, [value("a1_v", g, "V")])
            self.loop_step(pogo5, a, gnd_bad, fin)         # 2b) the data loop, relays at rest
            if not pogo5:                                  # 3) charging through the pads
                for sid in ("pads_charge", "pads_delivery"):
                    self.begin(sid)
                    fin(sid, FAIL, f"+5V pogo pin without contact (A0={a[0]} V)", f"A0 >= {POGO_5V_MIN_V:.1f} V at rest",
                        CLOCK.monotonic(), rest)
                return
            self.begin("pads_charge")
            t0 = CLOCK.monotonic()
            pads.relays(RELAY_CHARGE)
            sleep(CHARGE_SETTLE_S)
            a0, a2 = pads.adc(0), pads.adc(2)
            f, vbus, cur = pads.bat()
            bad = []
            if not a2 < CHARGE_A2_MAX_V:
                bad.append(f"A2={a2} V (must be < {CHARGE_A2_MAX_V:.1f})")
            if not a0 >= CHARGE_A0_MIN_V:
                bad.append(f"A0={a0} V (must be >= {CHARGE_A0_MIN_V:.1f})")
            if f.get("vbus") != "on":
                bad.append(f"vbus={f.get('vbus')}")
            if vbus is None or not CHARGE_VBUS_MIN_MV <= vbus <= CHARGE_VBUS_MAX_MV:
                bad.append(f"vbus_mv={vbus} ({CHARGE_VBUS_MIN_MV} to {CHARGE_VBUS_MAX_MV})")
            if cur is None or not cur > 0:
                bad.append(f"current_ma={cur} (must be > 0)")
            vals = [value("a0_v", a0, "V"), value("a2_v", a2, "V"), value("a3_v", a[3], "V"),
                    value("vbus_mv", vbus if vbus is not None else "", "mV"),
                    value("current_ma", cur if cur is not None else "", "mA")] + rest[:2]
            fin("pads_charge", FAIL if bad else PASS,
                f"A0={a0} V A2={a2} V vbus={f.get('vbus')} {vbus} mV {cur} mA" + ("; " + ", ".join(bad) if bad else ""),
                f"A2 < {CHARGE_A2_MAX_V:.1f} V; A0 >= {CHARGE_A0_MIN_V:.1f} V; "
                f"vbus {CHARGE_VBUS_MIN_MV}-{CHARGE_VBUS_MAX_MV} mV; current > 0", t0, vals, detail=str(f))
            if a0 < POGO_5V_MIN_V:
                pads.relays(RELAY_REST)
                self.begin("pads_delivery")
                fin("pads_delivery", NOTRUN, f"pads cut: A0={a0} V while charging", "", CLOCK.monotonic(),
                    [value("a0_v", a0, "V")])
                return
            self.delivery_step(pads, sleep, fin)           # 4) delivery, from charging
        except Exception as e:
            for sid in PADS_IDS:
                if sid not in done:
                    if not self.has(sid):
                        self.begin(sid)
                    fin(sid, NOVERDICT, f"{type(e).__name__}: {e}", "", t_blk, [], detail=traceback.format_exc())
        finally:
            pads.put_away(self.log)
            self.tick("rear_pads", t_blk)

    def loop_step(self, pogo5, a, gnd_bad, fin):
        """The data loop: a UART through pogo pins 2 and 3 (GPIO26/27)."""
        self.begin("pads_loop")
        t0 = CLOCK.monotonic()
        notes = []
        if not pogo5:
            notes.append(f"+5V pogo pin without contact (A0={a[0]} V)")
        if gnd_bad:
            notes.append(f"GND pogo pin without contact or doubtful (A1={gnd_bad} V)")
        note = ("; " + "; ".join(notes)) if notes else ""
        rng = "ok=1 and b9600, b115200, rev complete"
        try:
            recs = self.link.cmd(f"test_dot_loop count={LOOP_COUNT}", LOOP_TIMEOUT_S)
        except CommandError as e:
            if e.code == 1:
                fin("pads_loop", NOTRUN, "firmware without test_dot_loop" + note, rng, t0, [])
            else:
                fin("pads_loop", NOVERDICT, f"{e.term or 'no answer'}" + note, rng, t0, [])
            return
        f = record(recs, "loop")
        if not f:
            fin("pads_loop", FAIL, "test_dot_loop without its loop record" + note, rng, t0, [])
            return
        cnt = {k: f.get(k, "") for k in ("b9600", "b115200", "rev")}

        def whole(c):
            m = re.fullmatch(r"(\d+)/(\d+)", c)
            return bool(m) and m.group(1) == m.group(2) and int(m.group(2)) > 0
        ok = truthy(f.get("ok")) and all(whole(c) for c in cnt.values())
        why = f.get("why", "-")
        meas = (f"ok={f.get('ok')} b9600={cnt['b9600']} b115200={cnt['b115200']} rev={cnt['rev']}"
                + (f" why={why}" if why not in ("", "-") else ""))
        vals = [value(k, v) for k, v in cnt.items()] + [value("ms", f.get("ms", ""), "ms")]
        fin("pads_loop", PASS if ok else FAIL, meas + note, rng, t0, vals, detail=" ".join(recs))

    def delivery_step(self, pads, sleep, fin):
        """The Kode's boost into the station's load (6.6 ohm, about 0.71 A).
        `test_dot_boost state=on` raises the boost limit to 1.2 A for the test
        alone and `state=off` puts kodeOS's 0.5 A back."""
        self.begin("pads_delivery")
        t0 = CLOCK.monotonic()
        pads.relays(RELAY_DELIVERY)
        sleep(DELIVERY_SETTLE_S)
        s0 = pads.adc(0)
        if not s0 < DELIVERY_IDLE_MAX_V:
            fin("pads_delivery", FAIL, f"A0={s0} V without the boost (must be < {DELIVERY_IDLE_MAX_V:.1f})",
                f"A0 < {DELIVERY_IDLE_MAX_V:.1f} V without the boost",
                t0, [value("a0_no_boost_v", s0, "V")])
            return
        b = pads.boost_on()
        sleep(BOOST_SETTLE_S)
        e0 = pads.adc(0)
        f2, vbus2, cur2 = pads.bat()
        # The gauge's current while discharging: the boost's record and two readings, their median.
        curs = [c for c in (_int(b.get("current_ma")), cur2, pads.bat()[2]) if c is not None]
        cur = int(statistics.median_low(curs)) if curs else None
        bad = []
        if cur is None or not DELIVERY_CURRENT_MIN_MA <= cur <= DELIVERY_CURRENT_MAX_MA:
            bad.append(f"gauge: current_ma={cur} (discharge expected from {DELIVERY_CURRENT_MAX_MA} to "
                       f"{DELIVERY_CURRENT_MIN_MA}, negative)")
        if not DELIVERY_A0_MIN_V <= e0 <= DELIVERY_A0_MAX_V:
            bad.append(f"A0={e0} V ({DELIVERY_A0_MIN_V:.1f} to {DELIVERY_A0_MAX_V:.2f})")
        if not truthy(b.get("on")):
            bad.append(f"boost on={b.get('on')}")
        if not fault_ok(b.get("fault", "?")):
            bad.append(f"fault={b.get('fault')}")
        restored = pads.boost_off()
        if not restored:
            bad.append("the boost did not confirm off (and kodeOS's 0.5 A back)")
        vals = [value("a0_no_boost_v", s0, "V"), value("a0_v", e0, "V"),
                value("vbus_mv", vbus2 if vbus2 is not None else "", "mV"),
                value("current_ma", cur if cur is not None else "", "mA"), value("fault", b.get("fault", "")),
                value("boost_off", "yes" if restored else "NO")]
        fin("pads_delivery", FAIL if bad else PASS,
            f"A0={e0} V boost={b.get('on')} fault={b.get('fault')} limit 1.2 A, off={'yes' if restored else 'NO'}"
            + ("; " + ", ".join(bad) if bad else ""),
            f"A0 {DELIVERY_A0_MIN_V:.1f} to {DELIVERY_A0_MAX_V:.2f} V with the boost on (limit 1.2 A), no fault and "
            f"a gauge current from {DELIVERY_CURRENT_MAX_MA} to {DELIVERY_CURRENT_MIN_MA} mA",
            t0, vals, detail=str(f2))

    # ---- the radio -----------------------------------------------------------
    def radio_block(self):
        """The C5's card, Wi-Fi 2.4 and 5 GHz reception and BLE reception of
        the Mac's beacon, in one `test_dot_radio`."""
        self.done_bg.add("radio")
        w, b = self.cfg["wifi"], self.cfg["ble"]
        th = self.cfg.get("rssi_thresholds_dbm", {})
        t1 = CLOCK.monotonic()
        if not w.get("channel_24") or not w.get("channel_5"):
            for i in RADIO_IDS:
                self.begin(i)
                self.add(i, NOVERDICT, "the channels of the station's networks are missing: run "
                                       "test_dot_final.py --discover", values=[])
            return
        name = b["beacon_name"]
        beacon = make_beacon(name)
        err, recs, bt = None, [], {}
        try:
            bt = beacon.on()
            recs = self.link.cmd(f"test_dot_radio ssid24={quote(w['ssid_24'])} ch24={int(w['channel_24'])} "
                                 f"ssid5={quote(w['ssid_5'])} ch5={int(w['channel_5'])} ble={quote(name)} discover=off",
                                 RADIO_TIMEOUT_S)
        except CommandError as e:
            err = str(e)
        finally:
            beacon.off()
        t_cmd = CLOCK.monotonic() - t1
        card = record(recs, "radio_card") or {}
        rad = record(recs, "radio") or {}
        took = record(recs, "radio_time") or {}
        det = "\n".join(recs) + f"\nbeacon={bt}"

        def ms(k):
            return float(took.get(k, 0) or 0) / MS_PER_S

        if err or not rad:
            for i in RADIO_IDS:
                self.begin(i)
                self.add(i, TIMEOUT if err else FAIL, err or "test_dot_radio without its radio record", "", t_cmd,
                         detail=det, values=[])
            self.tick("radio", t1)
            return

        def over(key, rssi):
            return th.get(key) is None or (rssi is not None and rssi >= th[key])

        def step(sid, ok, measured, vals, secs):
            self.begin(sid)
            self.add(sid, PASS if ok else FAIL, measured, "", secs, detail=det, values=vals)

        self.extra["mac_c5"] = card.get("mac") if card.get("mac") not in (None, "-") else None
        self.extra["c5_fw"] = card.get("fw") if card.get("fw") not in (None, "-") else None
        step("c5", truthy(card.get("ok")),
             f"chip {card.get('chip')} id {card.get('id')}; fw {card.get('fw')} (host {card.get('host')}); "
             f"MAC {card.get('mac')}; SDIO {card.get('sdio')}",
             [value("chip", card.get("chip", "-")), value("fw", card.get("fw", "-")), value("mac", card.get("mac", "-")),
              value("sdio", card.get("sdio", "-")), value("time", ms("card_ms"), "s")], ms("card_ms"))
        for sid, band, ssid in (("wifi_24", "24", w["ssid_24"]), ("wifi_5", "5", w["ssid_5"])):
            seen = truthy(rad.get("seen" + band))
            rssi = _int(rad.get("rssi" + band)) if seen else None
            text = f"{ssid} " + (f"RSSI {rssi} dBm channel {rad.get('ch' + band)}" if seen else "not heard")
            if band == "5" and truthy(rad.get("passive5")):
                text += " (passive listening, a radar channel)"
            step(sid, seen and over(sid, rssi), text,
                 [value("rssi", rssi if seen else "-", "dBm"), value("channel", _int(rad.get("ch" + band)) if seen else "-"),
                  value("time", ms(f"w{band}_ms"), "s")], ms(f"w{band}_ms"))
        heard = truthy(rad.get("ble_rx"))
        if heard:
            rssi = _int(rad.get("rssi_rx"))
            ok = over("ble_rx", rssi)
            text = f"the Kode hears the beacon {name} RSSI {rssi} dBm"
            vals = [value("rssi", rssi, "dBm"), value("heard", "beacon")]
        else:
            ok = False
            text = f"the Kode does not hear the beacon {name}"
            vals = [value("rssi", "-", "dBm"), value("heard", "nothing")]
        step("ble_rx", ok, text, vals + [value("time", ms("ble_ms"), "s")], ms("ble_ms"))
        self.tick("radio", t1)

    def discover(self):
        """One sweep: the channels of the station's two networks, kept in the settings."""
        a, w, b = self.a, self.cfg["wifi"], self.cfg["ble"]
        self.link = Link(a.port)
        try:
            for _ in range(PING_TRIES):
                try:
                    self.link.cmd("ping", PING_TIMEOUT_S)
                    break
                except CommandError:
                    continue
            line = f"test_dot_radio ssid24={quote(w['ssid_24'])} ssid5={quote(w['ssid_5'])} ble={quote(b['beacon_name'])}"
            recs = self.link.cmd(line + " discover=on", DISCOVER_TIMEOUT_S)
        except CommandError as e:
            print(f"discover: {e}", flush=True)
            return 1
        finally:
            self.close()
        rad = record(recs, "radio") or {}
        print("\n".join(recs))
        if truthy(rad.get("seen24")):
            w["channel_24"] = int(rad["ch24"])
        if truthy(rad.get("seen5")):
            w["channel_5"] = int(rad["ch5"])
        save_config(self.config_path, self.cfg)
        print(f"2.4 GHz channel: {w['channel_24']}   5 GHz channel: {w['channel_5']}   (kept in {self.config_path})")
        return 0

    # ---- the exit record ------------------------------------------------------
    def exit_record(self):
        self.begin("exit_record")
        t1 = CLOCK.monotonic()
        try:
            self.read_exit()
            empty = [k for k, x in self.exit["values"].items() if x == ""]
            self.add("exit_record", PASS, f"{len(self.exit['values']) - len(empty)} values read"
                     + (f"; not read: {', '.join(empty)}" if empty else ""), "", CLOCK.monotonic() - t1,
                     detail=json.dumps(self.exit, ensure_ascii=False, indent=1)[-REPORT_DETAIL_CHARS:], values=[])
        except Exception as e:
            self.add("exit_record", NOVERDICT, f"{type(e).__name__}: {e}", "", CLOCK.monotonic() - t1,
                     detail=traceback.format_exc(), values=[])
        self.tick("record", t1)

    def read_exit(self):
        """How the Kode leaves, read with the ordinary verbs (bat_info, temp,
        the read-only rows pmic and gauge). A value that cannot be read stays
        empty, with its reason."""
        v, m = self.exit["values"], self.exit["reasons"]
        info = self.info

        def put(k, val, why):
            if val is None or val == "" or val == "-":
                v[k], m[k] = "", why
            else:
                v[k] = val

        put("serial", info.get("serial"), "info gave no serial")
        put("mac_p4", info.get("mac"), "info gave no MAC")
        put("kodeos", info.get("kodeos"), "info gave no kodeOS version")
        put("c5", info.get("c5"), "info gave no C5 version")
        put("mac_c5", self.extra.get("mac_c5"), "the C5's card was not read in this test")
        put("c5_fw", self.extra.get("c5_fw"), "the C5's card was not read in this test")
        try:
            f = next((fields(r) for r in self.link.cmd("bat_info") if "vbat_mv=" in r), {})
            put("cell_mv", f.get("vbat_mv"), "bat_info without vbat_mv")
            put("soc_pct", f.get("soc"), "bat_info without soc")
            put("current_ma", f.get("current_ma"), "bat_info without current_ma")
            put("vbus_mv", f.get("vbus_mv"), "bat_info without vbus_mv")
        except CommandError as e:
            for k in ("cell_mv", "soc_pct", "current_ma", "vbus_mv"):
                v[k], m[k] = "", f"bat_info: {e}"
        rows = {}
        for name in ("pmic", "gauge"):
            try:
                recs, logs = self.link.exchange(f"test component={name}", 20.0)
                rows[name] = "\n".join(recs + logs)
            except CommandError as e:
                rows[name] = "\n".join(e.records + e.logs)
                m["row_" + name] = str(e)
        self.exit["rows"] = rows
        p, g = rows.get("pmic", ""), rows.get("gauge", "")
        put("charger", _match(r"charge=(\S+)", p), "the pmic row gave no charge state")
        put("input", _match(r"input=(\S+)", p), "the pmic row gave no input")
        put("charger_faults", _match(r"faults=(0x\w+)", p), "the pmic row gave no faults")
        if v.get("cell_mv") == "":
            put("cell_mv", _match(r"vcell (\d+)mV", g), m.get("cell_mv", "not read"))
        tc = _match(r"temp (-?[\d.]+)C", g)
        no_sensor = re.search(r"temp -?[\d.]+C \(([^)]*)\)", g)
        if tc is not None and no_sensor is None:
            put("temp_cell_c", tc, "")
        else:
            put("temp_cell_c", None, (no_sensor.group(1) if no_sensor else "the gauge gave no temperature")
                + " (the gauge reads -40.0 without a sensor)")
        try:
            t = record(self.link.cmd("temp"), "temp=p4") or {}
            put("temp_p4_c", t.get("c"), "temp gave no reading")
        except CommandError as e:
            v["temp_p4_c"], m["temp_p4_c"] = "", f"temp: {e}"

    # ---- the end ------------------------------------------------------------
    def conclude(self, stop_why):
        done = {r["id"] for r in self.results}
        for sid in self.planned():
            if sid not in done:
                self.add(sid, NOTRUN, stop_why, values=[])
        verdict = self.verdict()
        if self.link is not None:
            verdict = self.tell_kode(verdict)
            try:
                self.save_photos()                     # read before the lock, which closes every verb
            except KeyboardInterrupt:
                self.interrupted = True
                self.log("interrupted while the photos were read", "error")
            if self.put_down(verdict) == LOCK_FAILED:
                verdict = FAIL                         # a Kode that is not locked does not leave as a PASS
        total = CLOCK.monotonic() - self.t0
        if self.op_end_t:
            self.blocks["after_operator"] = round(CLOCK.monotonic() - self.op_end_t, 2)
        shown = ("BENCH " + verdict) if self.bench else verdict
        ex = self.step("exit_record")
        if ex and ex["status"] == PASS and not self.bench:
            self.write_csv(total, shown)
        self.write_results(shown, total)
        self.write_html(shown, total)
        try:
            (self.out / "last_report.json").write_text(json.dumps(dict(
                serial=self.info["serial"], verdict=shown, t=CLOCK.time(), folder=self.outdir.name,
                report=str(self.outdir / "report.html"))))
        except OSError:
            pass
        screen_fails = [r["id"] for r in self.results if r["id"].startswith("cam_") and r["status"] in (FAIL, TIMEOUT)]
        extra = {"repeat_of": self.repeat_of} if self.repeat_of else {}
        self.emit(type="end", verdict=shown, total_s=round(total, 1), blocks=self.blocks,
                  report=str(self.outdir / "report.html"), repeatable=(shown != PASS), screen_fails=screen_fails,
                  lock=self.lock, **extra)
        print(f"\n{shown}  {total:.1f} s  {self.outdir}/report.html", flush=True)
        self.close()
        if not self.a.no_open:
            webbrowser.open((self.outdir / "report.html").as_uri())
        if self.events and not self.interrupted:      # a stopped run ends at once; the page is already back
            self.wait_removal()
        if self.lock == LOCK_FAILED:
            return EXIT_LOCK_FAILED
        return EXIT_PASS if verdict == PASS else EXIT_NOT_PASS

    def verdict(self):
        # The record never moves the verdict, and neither does an optional step.
        judged = [r for r in self.results if r["id"] != "exit_record" and r["id"] not in OPTIONAL]
        if any(r["status"] in (FAIL, TIMEOUT) for r in judged):
            return FAIL
        if any(r["status"] in (NOVERDICT, NOTRUN) for r in judged):
            return NOVERDICT
        return PASS

    def certificate(self):
        """The Kode's certificate, before its PASS: a Kode that gets none
        does not pass."""
        t0 = CLOCK.monotonic()
        self.begin(CERTIFICATE[0])
        try:
            said = certify(self.link, self)
        except CertificateRefused as e:
            self.add(CERTIFICATE[0], FAIL, str(e), CERTIFICATE_RANGE, CLOCK.monotonic() - t0, values=[],
                     name=CERTIFICATE[1])
            return FAIL
        self.add(CERTIFICATE[0], PASS, said, CERTIFICATE_RANGE, CLOCK.monotonic() - t0, values=[],
                 name=CERTIFICATE[1])
        return PASS

    def tell_kode(self, verdict):
        """The verdict on the Kode's glass. A PASS runs the factory reset and
        is a PASS only once the reset is done; where a certificate is issued,
        only once the Kode has it."""
        if verdict == PASS and not self.bench and certify is not None:
            verdict = self.certificate()
        word = "pass" if verdict == PASS else "fail"
        self.phase("result", "Result: " + ("ALL GOOD" if verdict == PASS else "FAULTS"), result=word)
        if self.bench:
            self.screen("result", word)
            return verdict
        if verdict != PASS:
            try:
                self.link.cmd("test_dot_result verdict=fail")
            except CommandError as e:
                self.log(f"test_dot_result verdict=fail: {e.term or 'no answer'}")
                self.screen("result", "fail")
            return verdict
        name = FACTORY_RESET[1]
        t1 = CLOCK.monotonic()
        try:
            self.link.cmd("test_dot_result verdict=pass", RESULT_PASS_TIMEOUT_S)
        except CommandError as e:
            if e.msg.startswith("reset_"):
                why = f"the factory reset stopped at its step {e.msg[len('reset_'):]}"
            elif e.term is None:
                why = "the Kode did not confirm the PASS (no answer)"
            else:
                why = f"the Kode refused the PASS: {e.msg or e.term}"
            self.begin(FACTORY_RESET[0])
            self.add(FACTORY_RESET[0], FAIL, why, "the factory reset completes", CLOCK.monotonic() - t1,
                     detail=e.term or "", values=[], name=name)
            return FAIL
        self.begin(FACTORY_RESET[0])
        self.add(FACTORY_RESET[0], PASS, "done; ship mode armed for the unplug", "the factory reset completes",
                 CLOCK.monotonic() - t1, values=[], name=name)
        return PASS

    def put_down(self, verdict):
        """A PASS is locked; a bench Kode is given back. A FAIL keeps its
        screen. A lock the Kode does not confirm is a failure of its own: the
        `lock` row, `lock` in results.json and the CSV, a `lock` event.

        @return The lock's state: LOCK_OK, LOCK_FAILED or LOCK_NOT_SENT."""
        if self.bench:
            try:
                self.link.cmd("test_dot_end", END_TIMEOUT_S)
            except CommandError as e:
                self.log(f"{e}", "error")
            return self.lock
        if verdict != PASS:
            return self.lock
        t0 = CLOCK.monotonic()
        try:
            self.link.cmd("test_dot_lock confirm=on", LOCK_TIMEOUT_S)
        except CommandError as e:
            why = f"the Kode did not confirm the lock: {e.term or 'no answer'}"
            self.lock = LOCK_FAILED
            self.begin("lock")
            self.add("lock", FAIL, why, "test_dot_lock answers >ok", CLOCK.monotonic() - t0,
                     detail=str(e), values=[], name=LOCK_STEP_NAME)
            self.log(f"lock: failed: {why}", "error")
            self.emit(type="lock", state=LOCK_FAILED, reason=why, t=CLOCK.time())
            return self.lock
        self.lock = LOCK_OK
        self.begin("lock")
        self.add("lock", PASS, "locked", "test_dot_lock answers >ok", CLOCK.monotonic() - t0, values=[],
                 name=LOCK_STEP_NAME)
        return self.lock

    def save_photos(self):
        """Every photo of the run (placement, marks, white, grey, red, ...) as
        a small JPEG beside the report, each checked by length and CRC; the
        missing or broken ones are asked for again one at a time, three
        rounds at most."""
        t0 = CLOCK.monotonic()
        got, total, retries, size = {}, None, 0, 0

        def ask(line):
            nonlocal total, size
            b = Blobs()
            try:
                recs = self.link.cmd(line, 90.0)
            except CommandError as e:
                self.log(f"{line}: {e.term or 'no answer'}")
                recs = e.records
            for r in recs:
                if b.feed(r):
                    continue
                f = fields(r)
                if r.startswith("photos ") and f.get("total", "").isdigit():
                    total = int(f["total"])
            b.flush()
            for name, data in b.good.items():
                mm = re.fullmatch(r"photo_(\d+)_[A-Za-z0-9_]+", name)
                if not mm or int(mm.group(1)) in got:
                    continue
                write_atomic(self.outdir / f"{name}.jpg", data)
                got[int(mm.group(1))] = name
                size += len(data)

        ask("test_dot_photos")
        for _ in range(PHOTO_ROUNDS):
            missing = [i for i in range(1, (total or 0) + 1) if i not in got]
            if not missing:
                break
            for i in missing:
                retries += 1
                ask(f"test_dot_photos only={i}")
        missing = [i for i in range(1, (total or 0) + 1) if i not in got]
        secs = round(CLOCK.monotonic() - t0, 1)
        self.photos = dict(saved=len(got), of=total, retries=retries, missing=missing, bytes=size, secs=secs)
        self.emit(type="photos", n=len(got), bytes=size, secs=secs, folder=str(self.outdir), retries=retries,
                  missing=missing, t=CLOCK.time())

    def write_results(self, shown, total):
        doc = dict(verdict=shown, serial=self.info["serial"], repeat_of=self.repeat_of, lock=self.lock, info=self.info,
                   total_s=round(total, 1), blocks=self.blocks, exit_record=self.exit, photos=self.photos,
                   steps={r["id"]: r for r in self.results})
        write_atomic(self.outdir / "results.json", json.dumps(doc, indent=1, ensure_ascii=False).encode())

    def write_csv(self, total, shown):
        v = self.exit["values"]
        station = str(self.cfg.get("station") or "")
        v["date_time"] = self.now_text()
        v["station"] = station
        if not station:
            self.exit["reasons"]["station"] = "the settings have no station"
        v["duration_s"] = round(total, 1)
        v["verdict"] = shown
        v["lock"] = self.lock
        head = [k for k, _ in EXIT_FIELDS]
        p = self.out / f"record_{station or 'no_station'}.csv"
        try:
            if p.exists():
                with open(p, newline="", encoding="utf-8") as fh:
                    if next(csv.reader(fh), None) != head:
                        stamp = datetime.fromtimestamp(CLOCK.time()).strftime("%Y%m%d-%H%M%S")
                        p.rename(p.with_name(f"{p.stem}_{stamp}.csv"))
            new = not p.exists()
            with open(p, "a", newline="", encoding="utf-8") as fh:
                w = csv.writer(fh)
                if new:
                    w.writerow(head)
                w.writerow([v.get(k, "") for k in head])
            self.exit["csv"] = str(p)
        except OSError as e:
            self.exit["csv_error"] = f"{type(e).__name__}: {e}"

    def write_html(self, verdict, total):
        e = html.escape
        rows = []
        for r in self.results:
            imgs = "".join(f'<a href="{e(i)}"><img src="{e(i)}" height="64"></a> ' for i in r["images"])
            det = f"<details><summary>detail</summary><pre>{e(r['detail'])}</pre></details>" if r["detail"] else ""
            rows.append(f'<tr><td>{r["n"]}</td><td>{e(r["name"])}</td><td style="background:{COLOURS.get(r["status"], "#444")};'
                        f'color:#fff;font-weight:600">{e(r["status"])}</td><td class="m">{e(r["measured"])}{det}</td>'
                        f'<td>{e(r["range"])}</td><td class="r">{r["secs"]:.1f} s</td><td>{imgs}</td></tr>')
        brows = "".join(f"<tr><td>{e(k)}</td><td class='r'>{v:.1f} s</td></tr>" for k, v in self.blocks.items())
        sv, sm = self.exit.get("values", {}), self.exit.get("reasons", {})
        exit_html = ""
        if sv:
            fl = "".join(f"<tr><td>{e(n)}</td><td><b>{e(str(sv.get(k, '')))}</b></td><td>{e(sm.get(k, ''))}</td></tr>"
                         for k, n in EXIT_FIELDS)
            exit_html = (f"<h3>How the Kode leaves</h3><table style='width:auto'><tr><th>Value</th><th>Reading</th>"
                         f"<th>If missing, why</th></tr>{fl}</table>")
        info = self.info
        rep = ""
        if self.repeat_of:
            rep = (f"<tr><td>Repeat</td><td colspan=3>Repeats the run {e(self.repeat_of['folder'])} "
                   f"({e(str(self.repeat_of.get('verdict')))}) with the same Kode: this last run counts</td></tr>")
        base = verdict.replace("BENCH ", "")
        doc = f"""<!doctype html><html lang="en"><meta charset="utf-8"><title>Final test {e(info['serial'])}</title>
<style>body{{font:14px -apple-system,Helvetica,Arial,sans-serif;margin:24px;color:#191919}}h1{{margin:0}}
.v{{display:inline-block;padding:6px 18px;border-radius:6px;color:#fff;font-size:22px;font-weight:700;background:{COLOURS.get(base, '#444')}}}
table{{border-collapse:collapse;width:100%;margin-top:16px}}td,th{{border:1px solid #ddd;padding:6px 8px;vertical-align:top;text-align:left}}
th{{background:#f4f4f6}}.m{{max-width:560px;word-break:break-word}}.r{{text-align:right;white-space:nowrap}}pre{{font-size:11px;white-space:pre-wrap}}
.h td{{border:0;padding:2px 14px 2px 0}}</style>
<h1>Kode Dot final test (E5)</h1><p><span class="v">{e(verdict)}</span></p>
<table class="h"><tr><td>Serial</td><td><b>{e(info['serial'])}</b></td><td>MAC P4</td><td>{e(info['mac'])}</td></tr>
<tr><td>kodeOS</td><td>{e(info['kodeos'])}</td><td>C5</td><td>{e(info['c5'])}</td></tr>
<tr><td>Date</td><td>{e(info['date'])}</td><td>Total time</td><td><b>{total:.1f} s</b></td></tr>
<tr><td>Lock</td><td colspan=3><b>lock: {e(self.lock)}</b></td></tr>{rep}</table>
<table><tr><th>#</th><th>Test</th><th>Result</th><th>Measured</th><th>Range</th><th>Time</th><th>Picture</th></tr>{''.join(rows)}</table>
{exit_html}
<h3>Time per block</h3><table style="width:auto">{brows}<tr><th>Total</th><th class="r">{total:.1f} s</th></tr></table></html>"""
        write_atomic(self.outdir / "report.html", doc.encode())

    def close(self):
        if self.link is not None:
            try:
                self.link.close()
            except Exception:
                pass
            self.link = None

    def wait_removal(self):
        """The port is closed: wait for the Kode to leave the USB, so the page
        goes back to Start by itself. A later run of the same Kode takes over
        and this one goes quiet."""
        t = CLOCK.monotonic()
        mine = self.outdir.name
        while CLOCK.monotonic() - t < REMOVAL_WAIT_S:
            gone = not port_present(self.a.port) or (
                REMOVAL_AFTER_S is not None and CLOCK.monotonic() - t >= REMOVAL_AFTER_S)
            if gone:
                self.emit(type="removed", t=CLOCK.time(), reason="usb")
                return
            CLOCK.sleep(0.5)
            try:
                if json.loads((self.out / "last_report.json").read_text()).get("folder") != mine:
                    return
            except (OSError, ValueError):
                pass
        self.emit(type="removed", t=CLOCK.time(), reason="time")


class Pads:
    """The station's relays and ADC, with the relay rule held on this side
    too, and the boost kept track of so the end always switches it off."""

    def __init__(self, link, sleep):
        self.link, self.sleep = link, sleep
        self.byte = None           # the last byte asked for (noted before it is sent)
        self.boost = False         # pessimistic: set before the boost is asked for

    def relays(self, b):
        relay_guard(self.byte, b)
        self.byte = b
        f = record(self.link.cmd(f"test_dot_relays state=0x{b:02X}"), "relays")
        if not f or not truthy(f.get("ok")):
            raise PadsError(f"relays 0x{b:02X}: {f or 'no record'}")
        self.sleep(RELAY_SETTLE_S)

    def adc(self, ch):
        f = record(self.link.cmd(f"test_dot_adc ch={ch}"), "adc")
        if not f or _int(f.get("mv")) is None:
            raise PadsError(f"adc {ch}: {f or 'no record'}")
        return round(_int(f["mv"]) / 1000.0 * (1 if ch == ADC_DIRECT_CH else 2), 3)

    def bat(self):
        f = next((fields(r) for r in self.link.cmd("bat_info") if "vbus_mv=" in r), {})
        return f, _int(f.get("vbus_mv")), _int(f.get("current_ma"))

    def boost_on(self):
        self.boost = True
        return record(self.link.cmd("test_dot_boost state=on", 10.0), "boost") or {}

    def boost_off(self):
        f = record(self.link.cmd("test_dot_boost state=off", 10.0), "boost") or {}
        if f and not truthy(f.get("on")):
            self.boost = False
            return True
        return False

    def put_away(self, log):
        """Boost off, then charging, then rest; each step tried whatever the last did."""
        steps = []
        if self.boost:
            steps.append(("boost off", self.boost_off))
        if self.byte is not None:
            steps += [("0xFE", lambda: self.relays(RELAY_CHARGE)), ("0xFF", lambda: self.relays(RELAY_REST))]
        for what, fn in steps:
            try:
                fn()
            except (PadsError, CommandError) as e:
                log(f"pads: could not put the station at rest ({what}): {e}")


def _int(v):
    try:
        return int(float(v))
    except (TypeError, ValueError):
        return None


def _match(rx, text):
    m = re.search(rx, text or "")
    return m.group(1) if m else None


def _sigterm(signum, frame):
    raise KeyboardInterrupt


def main(argv=None):
    ap = argparse.ArgumentParser(description="The E5 station's final test of a Kode Dot.")
    ap.add_argument("-p", "--port", default=DEFAULT_PORT)
    ap.add_argument("--events", action="store_true", help="one '@@TD {json}' line per event on stdout")
    ap.add_argument("--no-place", action="store_true", help="bench: skip the placement")
    ap.add_argument("--no-write", action="store_true", help="bench: NFC and RFID only read, nothing written")
    ap.add_argument("--operator-sim", action="store_true",
                    help="bench: the buttons and touches pressed through the console's injection; the verdict reads BENCH")
    ap.add_argument("--discover", action="store_true", help="find the channels of the station's two networks and exit")
    ap.add_argument("--out", default=str(HOME / "reports"), help="where the run folders and the CSV go")
    ap.add_argument("--config", default=str(HOME / "test_dot_station.json"), help="the station's settings")
    ap.add_argument("--no-open", action="store_true", help="do not open the report at the end")
    a = ap.parse_args(argv)
    return Station(a).run()


if __name__ == "__main__":
    sys.exit(main())
