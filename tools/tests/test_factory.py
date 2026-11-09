#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The station program's run, checked against a fake Kode Dot, no cable.

tools/factory/test_dot_final.py decides PASS or FAIL for a Kode Dot at the
end of the line, so what it sends and what it writes are held here to section
7 of the E5 spec: the verbs it speaks, the photos it insists on, the relay
bytes it never sends, the run it refuses, the report it links back to.

The board is tools/factory/ui/test_dot_sim.py's FakeKode, which replays the
records a Kode gave on the bench (board 6c:0a, 2026-11-03) in the station
verbs' English. Time is virtual: a run of a minute and a half takes well under
a second here, and a board that says nothing is judged by the same code.

pyserial is not needed: kcli only touches `serial.Serial` and
`serial.SerialException`, and both are stood in for below.

    python3 tools/tests/test_factory.py
"""

import ast
import contextlib
import csv
import io
import json
import os
import re
import shutil
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS / "factory"))
sys.path.insert(0, str(TOOLS / "factory" / "ui"))

if "serial" not in sys.modules:
    _serial = types.ModuleType("serial")
    _serial.Serial = None
    _serial.SerialException = type("SerialException", (OSError,), {})
    sys.modules["serial"] = _serial

import kcli  # noqa: E402
import test_dot_final as final  # noqa: E402
import test_dot_sim as sim  # noqa: E402
import reports  # noqa: E402
from kpayload import fields  # noqa: E402

FORBIDDEN = 0x01          # R1 closed with R2 open: the station's 5 V into the Kode's own
PADS_BYTES = [0xFF, 0xFB, 0xFF, 0xFE, 0xFC, 0xFE, 0xFF]
# The string keys among everything the station sends; every other key is bare.
QUOTED = {("test_dot_place", "win"), ("test_dot_radio", "ssid24"), ("test_dot_radio", "ssid5"),
          ("test_dot_radio", "ble"), ("test_dot_blob", "name"), ("nfc_write", "text"),
          ("rfid_write", "id")}


# Every bound the station program copies from kqp_config.h, by its name there.
SDK_BOUNDS = {
    "KODE_RESET_S": "KODE_TEST_DOT_RESET_MS",
    "KODE_LINE_MARGIN_S": "KODE_TEST_DOT_LINE_MARGIN_MS",
    "KODE_LINE_S": "KODE_TEST_DOT_LINE_MS",
    "KODE_ANSWER_S": "KODE_TEST_DOT_ANSWER_MS",
    "KODE_JOB_S": "KODE_TEST_DOT_JOB_MS",
    "KODE_PROBE_S": "KODE_RADIO_PROBE_MS",
    "KODE_RADIO_QUEUE_S": "KODE_TEST_DOT_RADIO_QUEUE_MS",
    "KODE_RADIO_S": "KODE_TEST_DOT_RADIO_MS",
    "KODE_SWEEP_S": "KODE_TEST_DOT_SWEEP_MS",
    "KODE_DISCOVER_MARGIN_S": "KODE_TEST_DOT_DISCOVER_MARGIN_MS",
    "KODE_DISCOVER_S": "KODE_TEST_DOT_DISCOVER_MS",
    "KODE_LOOP_S": "KODE_TEST_DOT_LOOP_MS",
    "KODE_END_S": "KODE_TEST_DOT_END_MS",
}


def c_defines(text):
    """The integer #defines of a C header, each expression worked out from
    the others: comments dropped, continuations joined, integer suffixes
    read off. A define that is not plain arithmetic is left out."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    text = text.replace("\\\n", " ")
    raw = {}
    for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)(?!\()[ \t]+([^\n]+)", text, re.M):
        raw[m.group(1)] = re.sub(r"\b(\d+)[uUlL]+\b", r"\1", m.group(2).strip())
    done = {}

    def value(name, seen=()):
        if name in done:
            return done[name]
        if name not in raw or name in seen:
            raise ValueError(name)
        expr = re.sub(r"[A-Za-z_][A-Za-z0-9_]*", lambda w: str(value(w.group(0), seen + (name,))), raw[name])
        tree = ast.parse(expr, mode="eval")
        allowed = (ast.Expression, ast.BinOp, ast.UnaryOp, ast.Constant, ast.Add, ast.Sub, ast.Mult,
                   ast.FloorDiv, ast.Div, ast.USub, ast.UAdd)
        if not all(isinstance(n, allowed) for n in ast.walk(tree)):
            raise ValueError(name)
        done[name] = eval(compile(tree, "<define>", "eval"))
        return done[name]

    for name in raw:
        try:
            value(name)
        except (ValueError, SyntaxError, TypeError, ZeroDivisionError, RecursionError):
            continue
    return done


class Run:
    """One station run against one fake Kode, in a temporary home."""

    def __init__(self, scenario, args=(), home=None):
        self.home = Path(home or tempfile.mkdtemp(prefix="test-dot-"))
        self.out = self.home / "reports"
        self.cfg = self.home / "test_dot_station.json"
        self.clock = sim.VirtualClock()
        self.fake = sim.FakeKode(scenario, self.clock)
        self.args = ["--port", sim.PORT, "--out", str(self.out), "--config", str(self.cfg),
                     "--no-open"] + list(args)
        self.stdout = ""
        self.rc = None
        sim.seed_config(self.cfg, final.TEMPLATE)

    def go(self):
        sim.install(self.fake, final)
        fake_time = types.SimpleNamespace(monotonic=self.clock.monotonic, sleep=self.clock.sleep,
                                          time=self.clock.time)
        buf = io.StringIO()
        with mock.patch.object(kcli, "time", fake_time), \
                mock.patch.object(final, "CLOCK", self.clock), \
                mock.patch.object(final, "REMOVAL_AFTER_S", 1.0), \
                mock.patch.object(final, "USB_WAIT_OVERRIDE_S", None), \
                contextlib.redirect_stdout(buf):
            self.rc = final.main(self.args)
        self.stdout = buf.getvalue()
        return self

    def folders(self):
        if not self.out.is_dir():
            return []
        return sorted(p for p in self.out.iterdir() if p.is_dir())

    def results(self, folder=None):
        folder = folder or self.folders()[-1]
        return json.loads((folder / "results.json").read_text())

    def events(self):
        out = []
        for line in self.stdout.splitlines():
            if line.startswith(final.EVENT_PREFIX + " "):
                out.append(json.loads(line[len(final.EVENT_PREFIX) + 1:]))
        return out

    def sent(self, verb):
        return [ln for ln in self.fake.lines if ln.split(" ", 1)[0] == verb]

    def cleanup(self):
        shutil.rmtree(self.home, ignore_errors=True)


class Base(unittest.TestCase):

    def run_case(self, scenario, args=(), home=None):
        r = Run(scenario, args, home).go()
        if home is None:
            self.addCleanup(r.cleanup)
        return r

    def assert_grammar(self, run):
        """Every line the station sent is in the console's grammar: a string
        quoted, nothing else ever."""
        for ln in run.fake.lines:
            verb, _, rest = ln.partition(" ")
            for key, val in fields(rest).items():
                raw = rest.split(key + "=", 1)[1]
                quoted = raw.startswith('"')
                if (verb, key) in QUOTED:
                    self.assertTrue(quoted, f"{key} unquoted in: {ln}")
                else:
                    self.assertFalse(quoted, f"{key} quoted in: {ln}")


class TestFullPass(Base):

    def test_full_pass_writes_report_and_csv(self):
        r = self.run_case(sim.Scenario("pass"))
        self.assertEqual(r.rc, 0, r.stdout[-2000:])
        folders = r.folders()
        self.assertEqual(len(folders), 1)
        folder = folders[0]
        self.assertTrue(folder.name.endswith("_" + sim.SERIAL), folder.name)
        res = r.results()
        self.assertEqual(res["verdict"], "PASS")
        self.assertEqual(res["serial"], sim.SERIAL)
        bad = {k: v["status"] for k, v in res["steps"].items() if v["status"] != "PASS"}
        self.assertEqual(bad, {})
        for step in ("place", "sd", "imu", "cam_framing", "cam_colour", "led", "buttons", "tp",
                     "nfc", "rfid", "wifi_24", "wifi_5", "ble_rx", "pads_charge", "pads_delivery",
                     "pads_gnd", "pads_loop", "mem", "exit_record"):
            self.assertIn(step, res["steps"])
        self.assertTrue((folder / "report.html").is_file())
        self.assertTrue((folder / "img_cam_location.jpg").is_file())
        self.assertTrue((folder / "img_cam_colour_strip.png").is_file())
        self.assertEqual(len(list(folder.glob("photo_*.jpg"))), len(sim.PHOTOS))
        # The CSV sits beside the run folders, one row per passed Kode.
        with open(r.out / "record_E5-01.csv", newline="") as f:
            rows = list(csv.DictReader(f))
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["serial"], sim.SERIAL)
        self.assertEqual(rows[0]["verdict"], "PASS")
        self.assertEqual(rows[0]["lock"], "ok")
        self.assertEqual(res["lock"], "ok")
        self.assertEqual(res["steps"]["lock"]["status"], "PASS")
        # A production run writes the NFC tag and reads it back.
        self.assertEqual(r.sent("nfc_write"), [f'nfc_write text="{sim.SERIAL}"'])
        # The pass is told to the Kode (it runs its factory reset), then locked.
        self.assertEqual(r.sent("test_dot_result"), ["test_dot_result verdict=pass"])
        self.assertEqual(r.sent("test_dot_lock"), ["test_dot_lock confirm=on"])
        self.assertTrue(r.fake.lines.index("test_dot_lock confirm=on")
                        > r.fake.lines.index("test_dot_photos"))
        self.assertIn(f'test_dot_place win="{sim.WINDOW}" cam_ms=10000 mark_ms=15000 dx_mm=15 deg=5',
                      r.fake.lines)
        self.assertIn("test component=all", r.fake.lines)
        self.assert_grammar(r)
        # Nothing of the run is written into the tree.
        self.assertEqual(sorted(p.name for p in (TOOLS / "factory").iterdir()
                                if p.suffix in (".csv", ".html") or p.name.startswith("2")), [])


class TestOptional(Base):

    def test_a_failed_nfc_is_reported_and_the_kode_still_passes(self):
        r = self.run_case(sim.Scenario("nfc"))
        self.assertEqual(r.rc, 0, r.stdout[-2000:])
        res = r.results()
        self.assertEqual(res["verdict"], "PASS")
        bad = {k: v["status"] for k, v in res["steps"].items() if v["status"] != "PASS"}
        self.assertEqual(bad, {"nfc": "FAIL"})
        self.assertEqual(r.sent("test_dot_result"), ["test_dot_result verdict=pass"])


class TestPhotos(Base):

    def test_missing_photo_is_asked_again_up_to_three_times(self):
        r = self.run_case(sim.Scenario("pass", photo_missing={5: 2, 7: 99}))
        self.assertEqual(r.rc, 0, r.stdout[-2000:])
        folder = r.folders()[0]
        self.assertEqual(r.sent("test_dot_photos").count("test_dot_photos only=5"), 2)
        self.assertEqual(r.sent("test_dot_photos").count("test_dot_photos only=7"), 3)
        self.assertTrue((folder / f"photo_05_{sim.PHOTOS[4]}.jpg").is_file())
        self.assertFalse((folder / f"photo_07_{sim.PHOTOS[6]}.jpg").exists())
        photos = r.results()["photos"]
        self.assertEqual(photos["missing"], [7])
        self.assertEqual(photos["saved"], len(sim.PHOTOS) - 1)
        # A photo is evidence, not a test: the verdict does not move.
        self.assertEqual(r.results()["verdict"], "PASS")

    def test_bad_crc_photo_is_asked_again(self):
        r = self.run_case(sim.Scenario("pass", photo_bad_crc={3: 1}))
        self.assertEqual(r.rc, 0, r.stdout[-2000:])
        folder = r.folders()[0]
        self.assertEqual(r.sent("test_dot_photos").count("test_dot_photos only=3"), 1)
        saved = folder / f"photo_03_{sim.PHOTOS[2]}.jpg"
        self.assertEqual(saved.read_bytes(), sim.make_jpeg(f"photo_03_{sim.PHOTOS[2]}"))
        self.assertEqual(r.results()["photos"]["missing"], [])
        self.assertFalse(list(folder.glob("*.part")))


class TestRelays(Base):

    def test_forbidden_relay_state_never_sent(self):
        # The guard alone: R1 closed with R2 open is refused before the wire.
        for prev, byte in ((0xFF, 0xFD), (0xFF, 0xF1), (0xFE, 0x01)):
            with self.assertRaises(final.PadsError):
                final.relay_guard(prev, byte)
        # Delivery is entered only from charging and left only to charging.
        with self.assertRaises(final.PadsError):
            final.relay_guard(0xFF, 0xFC)
        with self.assertRaises(final.PadsError):
            final.relay_guard(0xFC, 0xFF)
        for prev, byte in zip(PADS_BYTES, PADS_BYTES[1:]):
            final.relay_guard(prev, byte)
        # A whole run: the bytes on the wire are the pads sequence and nothing
        # the rule forbids.
        r = self.run_case(sim.Scenario("pass"))
        self.assertEqual(r.rc, 0, r.stdout[-2000:])
        sent = [int(fields(ln.split(" ", 1)[1])["state"], 16) for ln in r.sent("test_dot_relays")]
        self.assertEqual(sent, PADS_BYTES)
        self.assertEqual([b for b in sent if (b & 0x03) == FORBIDDEN], [])
        self.assertEqual(r.fake.relays_sent, PADS_BYTES)
        steps = r.results()["steps"]
        for s in ("pads_gnd", "pads_loop", "pads_charge", "pads_delivery"):
            self.assertEqual(steps[s]["status"], "PASS", (s, steps[s]))


class TestRefusal(Base):

    def test_refuses_a_kodeos_without_test_dot(self):
        r = self.run_case(sim.Scenario("pass", has_test_dot=False), ["--events"])
        self.assertEqual(r.rc, 2)
        self.assertEqual(r.folders(), [])
        self.assertEqual(r.sent("test"), [])
        self.assertEqual(r.sent("test_dot_place"), [])
        why = [e for e in r.events() if e.get("type") == "log" and e.get("level") == "error"]
        self.assertTrue(why, r.stdout)
        self.assertIn("test_dot", why[-1]["text"])


class TestRepeat(Base):

    def test_repeat_links_the_earlier_report(self):
        home = Path(tempfile.mkdtemp(prefix="test-dot-"))
        self.addCleanup(shutil.rmtree, home, True)
        first = self.run_case(sim.Scenario("fail"), home=home)
        self.assertEqual(first.rc, 1)
        self.assertEqual(first.results()["verdict"], "FAIL")
        earlier = first.folders()[0]
        # The second run starts some minutes after the first on the same Kode.
        second = Run(sim.Scenario("pass"), ["--events"], home)
        second.clock.now = first.clock.now + 300
        second.go()
        self.assertEqual(second.rc, 0, second.stdout[-2000:])
        later = [p for p in second.folders() if p != earlier]
        self.assertEqual(len(later), 1)
        res = second.results(later[0])
        self.assertEqual(res["repeat_of"]["folder"], earlier.name)
        self.assertEqual(res["repeat_of"]["verdict"], "FAIL")
        start = [e for e in second.events() if e.get("type") == "start"][0]
        self.assertEqual(start["repeat_of"]["folder"], earlier.name)
        self.assertIn(earlier.name, (later[0] / "report.html").read_text())
        # The simple reports read the same results.json: the failed run says
        # what fails in plain words, keeps the station's faults apart and
        # gives way to the repeat that counts.
        self.assertEqual(reports.write(second.out), 2)
        runs = {r["folder"]: r for r in reports.every(second.out)}
        self.assertEqual(runs[earlier.name]["counts"], later[0].name)
        self.assertEqual(runs[later[0].name]["earlier"], 1)
        self.assertIn("Screen: dead pixel", runs[earlier.name]["kode"])
        self.assertIn("Rear pads: the station's relays or its 5 V", runs[earlier.name]["station"])
        self.assertTrue((earlier / "simple.html").exists())
        self.assertIn(earlier.name, (second.out / "index.html").read_text())


class TestOperatorSim(Base):

    def test_operator_sim_drives_the_ui_events(self):
        r = self.run_case(sim.Scenario("pass", operator="injected"), ["--events", "--operator-sim"])
        self.assertEqual(r.rc, 0, r.stdout[-3000:])
        res = r.results()
        self.assertTrue(res["verdict"].startswith("BENCH "), res["verdict"])
        self.assertEqual(res["steps"]["buttons"]["status"], "PASS")
        self.assertEqual(res["steps"]["tp"]["status"], "PASS")
        # The presses and touches went down the console's injection verbs.
        ids = {fields(ln.split(" ", 1)[1])["id"] for ln in r.sent("btn")}
        self.assertEqual(ids, {"left", "bot", "down", "top", "right", "up"})
        self.assertTrue(r.sent("touch"))
        # And the page saw every prompt and every answer.
        ev = r.events()
        asks = [e for e in ev if e.get("type") == "operator" and e.get("kind") in ("press", "target")]
        self.assertEqual([e["what"] for e in asks if e["kind"] == "press"][:1], ["pad-left"])
        self.assertEqual({e["what"] for e in asks if e["kind"] == "press"}, set(sim.BUTTONS))
        self.assertEqual({e["what"] for e in asks if e["kind"] == "target"}, set(sim.TARGETS))
        phases = [e["name"] for e in ev if e.get("type") == "phase"]
        for p in ("place", "camera", "buttons", "touch", "result"):
            self.assertIn(p, phases)
        self.assertEqual([e["type"] for e in ev][0], "start")
        self.assertIn("end", [e["type"] for e in ev])
        # A bench run never runs the factory reset, never locks, never joins the CSV.
        self.assertEqual(r.sent("test_dot_result"), [])
        self.assertEqual(r.sent("test_dot_lock"), [])
        self.assertIn("test_dot_end", r.fake.lines)
        self.assertFalse((r.out / "record_E5-01.csv").exists())


class TestLock(Base):

    def test_a_refused_lock_is_a_failure_of_its_own(self):
        r = self.run_case(sim.Scenario("pass", lock_refused=True), ["--events"])
        self.assertEqual(r.rc, final.EXIT_LOCK_FAILED, r.stdout[-2000:])
        res = r.results()
        self.assertEqual(res["verdict"], "FAIL")
        self.assertEqual(res["lock"], "failed")
        self.assertEqual(res["steps"]["lock"]["status"], "FAIL")
        self.assertIn("lock: failed", (r.folders()[-1] / "report.html").read_text())
        with open(r.out / "record_E5-01.csv", newline="") as f:
            rows = list(csv.DictReader(f))
        self.assertEqual([(x["verdict"], x["lock"]) for x in rows], [("FAIL", "failed")])
        # The lock is asked before anything is published: the page hears the
        # failure, then an end that already says FAIL.
        types_ = [e["type"] for e in r.events()]
        self.assertLess(types_.index("lock"), types_.index("end"))
        lock = [e for e in r.events() if e["type"] == "lock"][0]
        self.assertEqual(lock["state"], "failed")
        end = [e for e in r.events() if e["type"] == "end"][0]
        self.assertEqual((end["verdict"], end["lock"]), ("FAIL", "failed"))
        self.assertLess(r.fake.lines.index("test_dot_photos"), r.fake.lines.index("test_dot_lock confirm=on"))


class TestBenchFlags(Base):

    def test_no_write_and_no_place_never_reset_or_lock(self):
        for flag in ("--no-write", "--no-place"):
            with self.subTest(flag=flag):
                r = self.run_case(sim.Scenario("pass"), [flag])
                res = r.results()
                # The fake's tags hold only what was written to them, so an
                # unwritten run reads nothing there; the bench is what matters.
                self.assertTrue(res["verdict"].startswith("BENCH "), res["verdict"])
                if flag == "--no-place":
                    self.assertEqual((r.rc, res["verdict"]), (final.EXIT_PASS, "BENCH PASS"))
                self.assertEqual(res["lock"], "not_sent")
                self.assertEqual(r.sent("test_dot_result"), [])
                self.assertEqual(r.sent("test_dot_lock"), [])
                self.assertIn("test_dot_end", r.fake.lines)
                self.assertFalse((r.out / "record_E5-01.csv").exists())
                if flag == "--no-write":
                    self.assertEqual(r.sent("nfc_write"), [])
                else:
                    self.assertEqual(r.sent("test_dot_place"), [])


class TestContract(Base):
    """What the Kode's records say and what the station reads, field by field
    (spec section 7; the contract fixes of 2026-11-03)."""

    def test_radio_card_reads_the_whole_card_and_its_time(self):
        r = self.run_case(sim.Scenario("pass"))
        self.assertEqual(r.rc, 0, r.stdout[-2000:])
        c5 = r.results()["steps"]["c5"]
        self.assertEqual(c5["status"], "PASS")
        self.assertIn("SDIO 4x40000kHz", c5["measured"])
        self.assertIn("fw 2.12.9 (host 2.12.9)", c5["measured"])
        # The times come from their own record, radio_time.
        self.assertAlmostEqual(c5["secs"], 0.12)
        ble = r.results()["steps"]["ble_rx"]
        self.assertEqual(ble["status"], "PASS")
        self.assertIn("hears the beacon", ble["measured"])

    def test_operator_timeouts_are_timeouts(self):
        r = self.run_case(sim.Scenario("timeouts"), ["--events"])
        steps = r.results()["steps"]
        self.assertEqual(steps["buttons"]["status"], final.TIMEOUT)
        self.assertEqual(steps["tp"]["status"], final.TIMEOUT)
        outs = [e for e in r.events() if e.get("type") == "operator" and e.get("kind") == "answer"]
        self.assertTrue(outs)
        for e in outs:
            self.assertEqual(e["result"], "timeout", e)
            self.assertIsNone(e["ms"], e)
            self.assertIsNone(e.get("got"), e)

    def test_a_give_up_is_a_failure_for_mistakes(self):
        a = final.argparse.Namespace(events=True, out="-", config="-", operator_sim=False, no_write=False,
                                     no_place=False, port="-", discover=False, no_open=True)
        with mock.patch.object(final, "load_config", return_value=json.loads(final.TEMPLATE.read_text())):
            st = final.Station(a)
        got = []
        st.emit = lambda **kw: got.append(kw)
        st.operator_event(fields("step=3 button=pad-down result=giveup pressed=top ms=812"))
        self.assertEqual((got[-1]["result"], got[-1]["reason"], got[-1]["ms"]), ("fail", "mistakes", 812))

    def test_the_station_waits_longer_than_the_kode(self):
        # Kode bound < console line < PC wait, for every verb the Kode bounds.
        self.assertLess(final.KODE_RESET_S, final.KODE_LINE_S)
        self.assertLess(final.KODE_LINE_S, final.RESULT_PASS_TIMEOUT_S)
        for kode, pc in ((final.KODE_RADIO_S, final.RADIO_TIMEOUT_S), (final.KODE_LOOP_S, final.LOOP_TIMEOUT_S),
                         (final.KODE_END_S, final.END_TIMEOUT_S), (final.KODE_DISCOVER_S, final.DISCOVER_TIMEOUT_S)):
            self.assertLess(kode, pc)
        # A discovery outlasts a whole sweep, and the line outlasts every bound.
        self.assertGreater(final.KODE_DISCOVER_S, final.KODE_SWEEP_S + final.KODE_RADIO_S)
        for kode in (final.KODE_DISCOVER_S, final.KODE_LOOP_S, final.KODE_END_S):
            self.assertLess(kode, final.KODE_LINE_S)

    def test_the_copied_bounds_match_the_sdk(self):
        """Each KODE_*_S here is a copy of a kqp_config.h bound: read the
        header the tree builds with (or KODE_SDK_DIR's) and hold every copy
        to it."""
        sdk = Path(os.environ.get("KODE_SDK_DIR", str(TOOLS.parent / "sdk")))
        header = sdk / "components" / "kqp" / "include" / "kqp_config.h"
        if not header.is_file():
            self.skipTest(f"no {header}")
        defs = c_defines(header.read_text())
        copies = {name for name in vars(final) if re.fullmatch(r"KODE_[A-Z0-9_]+_S", name)}
        self.assertEqual(copies, set(SDK_BOUNDS), "a KODE_*_S without its kqp_config.h name, or the reverse")
        for name, fw in sorted(SDK_BOUNDS.items()):
            with self.subTest(name=name):
                self.assertTrue(fw in defs, f"{header} has no {fw}: the SDK is older than the station program")
                self.assertAlmostEqual(getattr(final, name), defs[fw] / final.MS_PER_S, places=3,
                                       msg=f"{name} is not {fw}")

    def test_a_retried_photo_carries_no_total(self):
        fake = sim.FakeKode(sim.Scenario("pass"), sim.VirtualClock())
        fake.v_test_dot_photos({"only": "4"})
        said = [b.decode() for _, _, b in fake.out]
        self.assertFalse([x for x in said if "photos total=" in x], said)
        self.assertTrue([x for x in said if x.startswith(">|blob name=\"photo_04_")], said)

    def test_lock_refuses_all_but_the_end_then_everything_after_a_reboot(self):
        fake = sim.FakeKode(sim.Scenario("pass"), sim.VirtualClock())
        fake.ready_at = 0.0
        fake.handle("test_dot_lock confirm=on")
        fake.handle("test_dot_info")
        fake.handle("test_dot_end")
        said = [b.decode().strip() for _, _, b in fake.out]
        self.assertIn(">err code=5 msg=locked", said)
        self.assertEqual(said[-1], ">ok")
        fake.open()
        fake.ready_at = 0.0
        fake.out.clear()
        fake.handle("test_dot_end")
        self.assertEqual([b.decode().strip() for _, _, b in fake.out][-1], ">err code=5 msg=locked")


class TestInjectedTouch(Base):

    def test_touches_stay_on_the_glass(self):
        a = final.argparse.Namespace(events=False, out="-", config="-", operator_sim=True, no_write=False,
                                     no_place=False, port="-", discover=False, no_open=True)
        with mock.patch.object(final, "load_config", return_value=json.loads(final.TEMPLATE.read_text())):
            st = final.Station(a)
        st.rng = mock.Mock(random=lambda: 0.0, uniform=lambda lo, hi: lo, randint=lambda lo, hi: hi,
                           choice=lambda xs: xs[0])
        st.operator_sim({"step": "2", "of": "2", "target": "405,498"})
        lines = [ln for _, ln, _ in st.sim_queue]
        self.assertEqual(len(lines), 4)                  # one miss, then the touch
        for ln in lines:
            f = fields(ln.split(" ", 1)[1])
            self.assertTrue(0 <= int(f["x"]) <= 409 and 0 <= int(f["y"]) <= 501, ln)


if __name__ == "__main__":
    unittest.main(verbosity=2)
