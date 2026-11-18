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
          ("rfid_write", "id"), ("file_sha", "name"), ("test_dot_welcome", "name"),
          ("test_dot_welcome", "project"), ("test_dot_welcome", "sha"), ("test_dot_storage", "name"),
          ("test_dot_storage", "sha")}


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
        self.assertEqual(r.sent("nfc_write"), [f'nfc_write text="{final.NFC_OTHER_TEXT}"',
                                               f'nfc_write text="{sim.SERIAL}"'])
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


class TestAppsLeft(Base):
    """After the factory reset of a PASS, the Kode leaves with the Welcome alone."""

    def test_a_pass_checks_the_apps_after_the_reset_and_before_the_lock(self):
        r = self.run_case(sim.Scenario("pass"))
        res = r.results()
        self.assertEqual(res["verdict"], "PASS", r.stdout[-2000:])
        self.assertEqual(res["steps"]["apps_left"]["status"], "PASS")
        lines = r.fake.lines
        self.assertLess(lines.index("test_dot_result verdict=pass"), lines.index("app_list"))
        self.assertLess(lines.index("app_list"), lines.index("test_dot_lock confirm=on"))

    def test_another_app_left_on_the_kode_fails_it_unlocked(self):
        r = self.run_case(sim.Scenario("pass", apps=[("onboarding", sim.WELCOME_VERSION), ("kodecraft", "1.0.0")]))
        res = r.results()
        self.assertEqual(res["verdict"], "FAIL")
        self.assertEqual(res["steps"]["apps_left"]["status"], "FAIL")
        self.assertIn("kodecraft", res["steps"]["apps_left"]["measured"])
        self.assertEqual(r.sent("test_dot_lock"), [])

    def test_no_welcome_or_another_version_fails_it(self):
        for apps in ([], [("onboarding", "0.0.4")]):
            with self.subTest(apps=apps):
                res = self.run_case(sim.Scenario("pass", apps=apps)).results()
                self.assertEqual((res["verdict"], res["steps"]["apps_left"]["status"]), ("FAIL", "FAIL"))

    def test_the_bench_does_not_ask(self):
        r = self.run_case(sim.Scenario("pass"), ["--no-place"])
        self.assertEqual(r.sent("app_list"), [])


class TestExpectedVersion(Base):

    def run_expecting(self, version):
        r = Run(sim.Scenario())
        self.addCleanup(r.cleanup)
        cfg = json.loads(r.cfg.read_text())
        cfg["expect"] = {"kodeos": version}
        r.cfg.write_text(json.dumps(cfg))
        return r.go()

    def test_a_kode_with_another_kodeos_fails(self):
        r = self.run_expecting("99.99.99")
        res = r.results()
        self.assertEqual(res["steps"]["info"]["status"], "FAIL", res["steps"]["info"])
        self.assertIn("expects kodeOS 99.99.99", res["steps"]["info"]["measured"])
        self.assertEqual(res["verdict"], "FAIL")

    def test_the_expected_kodeos_passes(self):
        seen = self.run_case(sim.Scenario()).results()["info"]["kodeos"]
        r = self.run_expecting(seen)
        self.assertEqual(r.results()["verdict"], "PASS")


class TestNfc(Base):

    def test_a_failed_nfc_fails_the_kode(self):
        r = self.run_case(sim.Scenario("nfc"))
        self.assertEqual(r.rc, 1, r.stdout[-2000:])
        res = r.results()
        self.assertEqual(res["verdict"], "FAIL")
        bad = {k: v["status"] for k, v in res["steps"].items() if v["status"] != "PASS"}
        self.assertEqual(bad, {"nfc": "FAIL"})
        self.assertEqual(r.sent("test_dot_result"), ["test_dot_result verdict=fail"])
        # No tag in the field is not written again.
        self.assertEqual(len(r.sent("nfc_write")), 1)

    def nfc_value(self, step, name):
        return next(v["value"] for v in step["values"] if v["name"] == name)

    def test_a_clean_nfc_says_one_try_each(self):
        nfc = self.run_case(sim.Scenario()).results()["steps"]["nfc"]
        self.assertEqual(nfc["status"], "PASS", nfc)
        self.assertRegex(nfc["measured"], r"write \d+\.\d s, tries 1\+1 \(other text, serial\); read \d+\.\d s, 1 try")
        self.assertNotIn("once more", nfc["measured"])
        self.assertEqual((self.nfc_value(nfc, "write_tries"), self.nfc_value(nfc, "read_tries")), (2, 1))

    def test_a_write_that_fails_once_on_the_tag_is_written_again(self):
        r = self.run_case(sim.Scenario(nfc_write_fails=1))
        nfc = r.results()["steps"]["nfc"]
        self.assertEqual(nfc["status"], "PASS", nfc)
        self.assertEqual(self.nfc_value(nfc, "write_retries"), 1)
        self.assertIn("tries 2+1 (other text, serial)", nfc["measured"])
        self.assertEqual(self.nfc_value(nfc, "write_tries"), 3)
        texts = [fields(ln.partition(" ")[2])["text"] for ln in r.sent("nfc_write")]
        self.assertEqual(texts, [final.NFC_OTHER_TEXT, final.NFC_OTHER_TEXT, r.results()["serial"]])

    def test_a_write_that_fails_twice_fails_the_kode(self):
        r = self.run_case(sim.Scenario(nfc_write_fails=2))
        nfc = r.results()["steps"]["nfc"]
        self.assertEqual(nfc["status"], "FAIL", nfc)
        self.assertEqual(len(r.sent("nfc_write")), 2)

    def test_a_serial_cut_short_on_the_tag_is_written_once_more(self):
        r = self.run_case(sim.Scenario(nfc_truncated=1))
        nfc = r.results()["steps"]["nfc"]
        self.assertEqual(nfc["status"], "PASS", nfc)
        self.assertEqual(self.nfc_value(nfc, "write_retries"), 1)
        self.assertIn("tries 1+2 (other text, serial)", nfc["measured"])
        self.assertIn("2 tries", nfc["measured"].partition("; read")[2])
        serial = r.results()["serial"]
        self.assertIn(f'first read back "{serial[:len(final.NFC_OTHER_TEXT)]}": the serial written and read once more',
                      nfc["measured"])
        self.assertEqual((self.nfc_value(nfc, "write_tries"), self.nfc_value(nfc, "read_tries")), (3, 2))
        texts = [fields(ln.partition(" ")[2])["text"] for ln in r.sent("nfc_write")]
        self.assertEqual(texts, [final.NFC_OTHER_TEXT, serial, serial])
        self.assertEqual(len(r.sent("nfc_read")), 2)
        self.assertIn(serial[:len(final.NFC_OTHER_TEXT)], nfc["detail"])

    def test_a_serial_cut_short_twice_fails_the_kode_after_one_more_write(self):
        r = self.run_case(sim.Scenario(nfc_truncated=2))
        nfc = r.results()["steps"]["nfc"]
        self.assertEqual(nfc["status"], "FAIL", nfc)
        self.assertEqual(r.results()["verdict"], "FAIL")
        self.assertEqual(len(r.sent("nfc_write")), 3)
        self.assertEqual(len(r.sent("nfc_read")), 2)

    def test_a_read_back_that_fails_once_is_written_and_read_once_more(self):
        r = self.run_case(sim.Scenario(nfc_read_fails=1))
        nfc = r.results()["steps"]["nfc"]
        self.assertEqual(nfc["status"], "PASS", nfc)
        self.assertEqual(self.nfc_value(nfc, "write_retries"), 1)
        self.assertEqual(len(r.sent("nfc_write")), 3)
        self.assertEqual(len(r.sent("nfc_read")), 2)
        self.assertIn('first read back "nothing": the serial written and read once more', nfc["measured"])

    def test_a_read_back_that_fails_twice_fails_the_kode_after_one_more_write(self):
        r = self.run_case(sim.Scenario(nfc_read_fails=2))
        nfc = r.results()["steps"]["nfc"]
        self.assertEqual(nfc["status"], "FAIL", nfc)
        self.assertEqual(len(r.sent("nfc_write")), 3)
        self.assertEqual(len(r.sent("nfc_read")), 2)

    def test_the_serial_is_written_after_another_text(self):
        r = self.run_case(sim.Scenario())
        texts = [fields(ln.partition(" ")[2])["text"] for ln in r.sent("nfc_write")]
        self.assertEqual(texts, [final.NFC_OTHER_TEXT, r.results()["serial"]])
        self.assertEqual(r.results()["steps"]["nfc"]["status"], "PASS")


class TestRfidWrite(Base):

    def test_the_id_that_counts_is_written_after_another_one(self):
        r = self.run_case(sim.Scenario())
        ids = [fields(ln.partition(" ")[2])["id"] for ln in r.sent("rfid_write")]
        self.assertEqual(len(ids), 2, ids)
        self.assertNotEqual(ids[0], ids[1])
        self.assertEqual(ids[1], final.rfid_id_of(r.results()["info"]["mac"]))
        self.assertEqual(r.results()["steps"]["rfid"]["status"], "PASS")

    def test_a_tag_that_already_holds_the_id_does_not_pass_a_dead_write(self):
        own = final.rfid_id_of(self.run_case(sim.Scenario()).results()["info"]["mac"])
        r = self.run_case(sim.Scenario(rfid_tag=own, rfid_dead_write=True))
        res = r.results()
        self.assertEqual(res["steps"]["rfid"]["status"], "FAIL", res["steps"]["rfid"])
        self.assertEqual(res["verdict"], "FAIL")
        self.assertEqual(r.sent("test_dot_result"), ["test_dot_result verdict=fail"])


    def test_a_busy_reader_is_waited_for_and_does_not_use_up_the_tries(self):
        # rep_3: a clone answered past its deadline, then every rfid verb was
        # refused as busy within 9 ms, and a good Kode failed.
        r = self.run_case(sim.Scenario(rfid_late_clone=True, rfid_busy_s=1.0))
        rfid = r.results()["steps"]["rfid"]
        self.assertEqual(rfid["status"], "PASS", rfid)
        busy = next(v["value"] for v in rfid["values"] if v["name"] == "busy_refusals")
        self.assertIn(f"reader busy {busy} times", rfid["measured"])
        self.assertIn("tries 2+1 (other ID, own ID)", rfid["measured"])
        self.assertGreater(busy, 0)
        ids = [fields(ln.partition(" ")[2])["id"] for ln in r.sent("rfid_write")]
        self.assertEqual(ids[-1], final.rfid_id_of(r.results()["info"]["mac"]))

    def test_a_reader_that_stays_busy_fails_as_busy_within_its_bound(self):
        r = self.run_case(sim.Scenario(rfid_late_clone=True, rfid_busy_s=999.0))
        rfid = r.results()["steps"]["rfid"]
        self.assertEqual(rfid["status"], "FAIL", rfid)
        self.assertIn("stayed busy", rfid["measured"])
        self.assertLess(rfid["secs"], 3.1 + final.RFID_BUSY_S + 2.0, rfid["secs"])

    def test_a_clean_rfid_says_one_try_each(self):
        rfid = self.run_case(sim.Scenario()).results()["steps"]["rfid"]
        self.assertEqual(rfid["status"], "PASS", rfid)
        self.assertRegex(rfid["measured"], r"write \d+\.\d s, tries 1\+1 \(other ID, own ID\) \(checked by the Kode: yes\), "
                                           r"read \d+\.\d s, 1 try")
        self.assertNotIn("busy", rfid["measured"])
        vals = {v["name"]: v["value"] for v in rfid["values"]}
        self.assertEqual((vals["write_tries"], vals["read_tries"], vals["busy_refusals"]), (2, 1, 0))

    def test_a_clone_not_read_back_is_written_up_to_three_times(self):
        r = self.run_case(sim.Scenario(rfid_dead_write=True))
        self.assertEqual(len(r.sent("rfid_write")), 2 * final.RFID_WRITE_TRIES)
        rfid = r.results()["steps"]["rfid"]
        n = final.RFID_WRITE_TRIES
        self.assertIn(f"tries {n}+{n} (other ID, own ID) (checked by the Kode: no)", rfid["measured"])
        self.assertEqual(final.RFID_WRITE_TRIES, 3)


class TestCameraLost(Base):

    def test_a_camera_that_lost_its_settings_once_is_woken_again_and_passes(self):
        # r1st_1: the fixture's camera kept losing its settings; the station
        # blamed the seating. Now J3 goes off and on and the placement repeats.
        r = self.run_case(sim.Scenario(case="cam_lost_once"))
        self.assertEqual(len(r.sent("test_dot_place")), 2)
        self.assertEqual(r.results()["steps"]["place"]["status"], "PASS")
        self.assertTrue(r.results()["place_again"], "the second placement is not counted")
        self.assertIn("placement_lost", r.results()["blocks"])
        # J3 goes off between the two placements.
        self.assertEqual(len(r.sent("test_dot_end")) >= 1, True)

    def test_a_placement_without_the_camera_lost_is_not_counted_again(self):
        r = self.run_case(sim.Scenario())
        self.assertFalse(r.results()["place_again"])

    def test_a_camera_that_keeps_losing_them_is_the_station_not_the_seating(self):
        r = self.run_case(sim.Scenario(case="cam_lost"))
        self.assertEqual(len(r.sent("test_dot_place")), 2)
        place = r.results()["steps"]["place"]
        self.assertEqual(place["status"], "NO VERDICT", place)
        self.assertIn("keeps losing its settings", place["measured"])
        self.assertNotIn("badly seated", place["measured"])


class TestFraming(Base):
    """The camera window follows the screen; the framing is the fixture's, never the Kode's."""

    def test_the_window_is_worked_out_as_the_bench_found_it(self):
        # The bench's answers: an offset of -0.67 / -0.74 mm centres at 750,568, one of -0.82 / -2.94 mm at 746,510.
        self.assertEqual(final.centred_window(-0.67, -0.74), [750, 568])
        self.assertEqual(final.centred_window(-0.82, -2.94), [746, 510])
        self.assertEqual(final.centred_window(*sim.OFFSET_MM), [int(v) for v in sim.WINDOW.split(",")])
        # Never outside the array, always even.
        self.assertEqual(final.centred_window(-40, 40), [0, 1176])

    def test_the_margin_has_the_sign_the_kode_judged_on_the_bench(self):
        # Two bench records at the old window 774,600: just in (inside=1) and just out (inside=0).
        m_in = final.band_margins(-0.91, -1.68, 2.147, [774, 600], 1.43)
        m_out = final.band_margins(-0.80, -2.02, 2.149, [774, 600], 1.37)
        self.assertGreater(min(m_in.values()), 0)
        self.assertLess(min(m_out.values()), 0)
        self.assertEqual(min(m_out, key=m_out.get), "top")
        # Centred on the second one, the band is well inside.
        self.assertGreater(min(final.band_margins(-0.80, -2.02, 2.149, final.centred_window(-0.80, -2.02),
                                                  1.37).values()), 30)

    def test_a_centred_kode_is_placed_once_and_costs_nothing(self):
        r = self.run_case(sim.Scenario())
        self.assertEqual(r.rc, 0, r.stdout[-2000:])
        res = r.results()
        self.assertEqual(r.sent("test_dot_place"), [f'test_dot_place win="{sim.WINDOW}" cam_ms=10000 mark_ms=15000 '
                                                    'dx_mm=15 deg=5'])
        ln = r.fake.lines
        self.assertNotIn("test_dot_end", ln[:ln.index("test component=all")])
        self.assertIsNone(res["recentred"])
        self.assertNotIn("placement_recentre", res["blocks"])
        framing = res["steps"]["cam_framing"]
        self.assertEqual(framing["status"], "PASS")
        self.assertRegex(framing["measured"], r"^band margin \+\d+ px at the ")
        self.assertIn("band_margin", [v["name"] for v in framing["values"]])

    def test_a_kode_3_mm_off_centre_gets_the_window_centred_and_passes(self):
        r = self.run_case(sim.Scenario(case="off_centre"))
        self.assertEqual(r.rc, 0, r.stdout[-2000:])
        res = r.results()
        self.assertEqual(res["verdict"], "PASS")
        new = final.centred_window(*r.fake.sc.offset_mm)
        self.assertEqual([ln.split(" ")[1] for ln in r.sent("test_dot_place")],
                         [f'win="{sim.WINDOW}"', f'win="{new[0]},{new[1]}"'])
        # The camera opens its window when it wakes: the verbs end in between.
        ln = r.fake.lines
        first, second = [i for i, x in enumerate(ln) if x.startswith("test_dot_place")]
        self.assertIn("test_dot_end", ln[first:second])
        self.assertEqual(res["recentred"]["before"], [int(v) for v in sim.WINDOW.split(",")])
        self.assertEqual(res["recentred"]["after"], new)
        self.assertTrue(res["recentred"]["ok"])
        self.assertGreater(res["blocks"]["placement_recentre"], 0)
        self.assertIn(f"-> {new[0]},{new[1]}", res["steps"]["place"]["measured"])
        self.assertEqual(res["steps"]["cam_framing"]["status"], "PASS")
        self.assertEqual(json.loads(r.cfg.read_text())["place"]["window"], new)
        # The station keeps the window: the next Kode in the same spot is placed once.
        again = Run(sim.Scenario(case="off_centre"), home=r.home).go()
        self.assertEqual(again.rc, 0, again.stdout[-2000:])
        self.assertEqual(again.sent("test_dot_place")[0].split(" ")[1], f'win="{new[0]},{new[1]}"')
        self.assertEqual(len(again.sent("test_dot_place")), 1)
        self.assertIsNone(again.results()["recentred"])

    def test_without_the_recentre_the_same_kode_would_not_fit(self):
        # The simulator's framing is worked out, not canned: the old window leaves the band out.
        sc = sim.Scenario(case="off_centre")
        self.assertFalse(sc.framing())
        sc.win = sc.sensor_win = ",".join(str(v) for v in final.centred_window(*sc.offset_mm))
        self.assertTrue(sc.framing())

    def test_a_new_window_without_waking_the_camera_again_changes_nothing(self):
        # The camera opens its window as it wakes: a second placement without the end
        # keeps the old window, and the band stays out.
        with mock.patch.object(final.Station, "place_fresh",
                               lambda self, win: self.place_look(win)):
            r = self.run_case(sim.Scenario(case="off_centre"))
        self.assertEqual(len(r.sent("test_dot_place")), 2)
        self.assertNotEqual(r.results()["steps"]["cam_framing"]["status"], "PASS")

    def test_a_band_that_fits_no_window_is_the_station_never_a_fail(self):
        r = self.run_case(sim.Scenario(case="camera_off"))
        res = r.results()
        self.assertEqual(res["verdict"], "NO VERDICT", res["steps"]["cam_framing"])
        self.assertEqual(len(r.sent("test_dot_place")), 2, "one recentre, no more")
        self.assertTrue(res["recentred"]["ok"])
        framing = res["steps"]["cam_framing"]
        self.assertEqual(framing["status"], "NO VERDICT")
        self.assertTrue(framing["measured"].startswith("station: the fixture's camera is off centre, recalibrate"),
                        framing["measured"])
        self.assertNotIn("FAIL", [s["status"] for s in res["steps"].values()])
        # The other camera rows judge as ever.
        for i in ("cam_pixels", "cam_lines", "cam_zones", "cam_colour"):
            self.assertEqual(res["steps"][i]["status"], "PASS", i)
        # The operator reads it as the station's.
        words, kind = reports.phrases(dict(framing, id="cam_framing"))
        self.assertEqual(kind, "station")
        self.assertIn("recalibrate", words[0])

    def test_a_framing_fail_from_the_kode_is_never_a_fail(self):
        # A Kode whose window was already centred and the band still out: no recentre, NO VERDICT.
        r = self.run_case(sim.Scenario(case="camera_off", offset_mm=sim.OFFSET_MM))
        res = r.results()
        self.assertEqual(len(r.sent("test_dot_place")), 1)
        self.assertEqual(res["steps"]["cam_framing"]["status"], "NO VERDICT")
        self.assertEqual(res["verdict"], "NO VERDICT")


class TestPhotos(Base):

    def test_missing_photo_is_asked_again_up_to_three_times(self):
        r = self.run_case(sim.Scenario("pass", photo_missing={5: 2, 7: 99}))
        self.assertEqual(r.rc, 0, r.stdout[-2000:])
        folder = r.folders()[0]
        self.assertEqual(r.sent("test_dot_photos").count("test_dot_photos only=5"), 2)
        # Three rounds beside the operator, and the read done again at the end
        # because one is still missing: three more for that one.
        self.assertEqual(r.sent("test_dot_photos").count("test_dot_photos only=7"), 6)
        self.assertTrue((folder / f"photo_05_{sim.PHOTOS[4]}.jpg").is_file())
        self.assertFalse((folder / f"photo_07_{sim.PHOTOS[6]}.jpg").exists())
        photos = r.results()["photos"]
        self.assertEqual(photos["missing"], [7])
        self.assertEqual(photos["saved"], len(sim.PHOTOS) - 1)
        # A photo is evidence, not a test: the verdict does not move.
        self.assertEqual(r.results()["verdict"], "PASS")

    def test_the_photos_are_read_beside_the_operator_once(self):
        r = self.run_case(sim.Scenario("pass"))
        self.assertEqual(r.sent("test_dot_photos"), ["test_dot_photos"])
        lines = r.fake.lines
        self.assertLess(lines.index("test_dot_photos"), lines.index("test_dot_result verdict=pass"))
        self.assertIn("parallel/photos", r.results()["blocks"])
        self.assertEqual(r.results()["photos"]["saved"], len(sim.PHOTOS))

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
        # The operator light (R4) rides on the pads' byte and is written on its own
        # when it goes on and off: leave its bit out and a repeat is not a change.
        pads = [sent[0] | 0x08]
        for b in sent[1:]:
            if b | 0x08 != pads[-1]:
                pads.append(b | 0x08)
        self.assertEqual(pads, PADS_BYTES)
        self.assertEqual([b for b in sent if (b & 0x03) == FORBIDDEN], [])
        self.assertEqual([b for b in r.fake.relays_sent if (b & 0x03) == FORBIDDEN], [])
        steps = r.results()["steps"]
        for s in ("pads_gnd", "pads_loop", "pads_charge", "pads_delivery"):
            self.assertEqual(steps[s]["status"], "PASS", (s, steps[s]))

    def test_the_charge_row_waits_for_the_charger_and_takes_a_full_cell(self):
        # Near full, the charger starts again seconds after the 5 V reaches
        # the pads: the first readings show no current, a later one does.
        r = self.run_case(sim.Scenario("pass", pads_charger="slow"))
        st = r.results()["steps"]["pads_charge"]
        self.assertEqual(st["status"], "PASS", st)
        self.assertGreaterEqual(len(r.sent("bat_info")), 4)
        # A full cell takes no current at all: the charger says its charge is
        # done, and the 5 V it sees on the pads is the proof the row is after.
        r = self.run_case(sim.Scenario("pass", pads_charger="full"))
        st = r.results()["steps"]["pads_charge"]
        self.assertEqual(st["status"], "PASS", st)
        self.assertIn("charge done", st["measured"])
        # No current and no charge done is still a failure, after the wait.
        r = self.run_case(sim.Scenario("pass", pads_charger="idle"))
        st = r.results()["steps"]["pads_charge"]
        self.assertEqual(st["status"], "FAIL", st)
        self.assertIn("current_ma=-4", st["measured"])

    def test_the_delivery_row_takes_the_boost_from_bat_info_too(self):
        # The verb's record may be the charger before its next poll: on=0
        # with the boost on. `bat_info` after the settle says it.
        r = self.run_case(sim.Scenario("pass", boost_late=True))
        st = r.results()["steps"]["pads_delivery"]
        self.assertEqual(st["status"], "PASS", st)

    @staticmethod
    def boost_times(r):
        """When the boost went on and off, and the A0 readings in between."""
        on = next(t for t, ln in r.fake.sent_at if ln == "test_dot_boost state=on")
        off = next(t for t, ln in r.fake.sent_at if ln == "test_dot_boost state=off")
        a0 = [t for t, ln in r.fake.sent_at if ln == "test_dot_adc ch=0" and on < t < off]
        return on, off, a0

    def test_the_delivery_row_reads_the_boost_again_before_it_switches_it_off(self):
        r = self.run_case(sim.Scenario("pass"))
        st = r.results()["steps"]["pads_delivery"]
        self.assertEqual(st["status"], "PASS", st)
        on, off, a0 = self.boost_times(r)
        self.assertEqual(len(a0), 2, a0)
        self.assertGreaterEqual(a0[1] - on, final.DELIVERY_LATE_S)
        bat = [t for t, ln in r.fake.sent_at if ln == "bat_info" and a0[1] < t < off]
        self.assertEqual(len(bat), 1)
        vals = {v["name"]: v["value"] for v in st["values"]}
        self.assertAlmostEqual(vals["a0_late_v"], 4.64, 2)
        self.assertEqual(vals["current_late_ma"], -1150)
        self.assertGreaterEqual(vals["t_late"], final.DELIVERY_LATE_S)
        self.assertIn("then A0=4.64 V -1150 mA at 2.5 s", st["measured"])

    def test_a_boost_that_falls_under_the_load_fails_at_the_second_reading(self):
        r = self.run_case(sim.Scenario("pass", boost_falls_s=1.5))
        st = r.results()["steps"]["pads_delivery"]
        self.assertEqual(st["status"], "FAIL", st)
        self.assertIn("A0=0.0 V 2.5 s after the boost on", st["measured"])
        self.assertIn("current_ma=-20 2.5 s after the boost on", st["measured"])
        self.assertFalse(r.fake.boost)

    def test_a_boost_that_fails_at_the_first_reading_is_not_waited_for(self):
        r = self.run_case(sim.Scenario("pass", boost_falls_s=0.1))
        st = r.results()["steps"]["pads_delivery"]
        self.assertEqual(st["status"], "FAIL", st)
        on, off, a0 = self.boost_times(r)
        self.assertEqual(len(a0), 1, a0)
        self.assertLess(off - on, final.DELIVERY_LATE_S)
        self.assertNotIn("a0_late_v", [v["name"] for v in st["values"]])


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
        self.assertNotIn("test_dot_result verdict=pass", r.fake.lines)
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


class TestOperatorLight(Base):

    def test_the_light_is_on_for_the_buttons_and_touch_and_off_after(self):
        r = self.run_case(sim.Scenario("pass"))
        self.assertEqual(r.rc, 0, r.stdout[-2000:])
        # On while the operator works, off at the end, and every byte on the wire
        # says so in its bit.
        self.assertTrue(any(r.fake.light))
        self.assertFalse(r.fake.light[-1])
        sent = [int(fields(ln.split(" ", 1)[1])["state"], 16) for ln in r.sent("test_dot_relays")]
        for b, lit in zip(sent, r.fake.light):
            self.assertEqual((b & 0x08) == 0, lit)

    def test_the_light_is_off_when_the_run_stops(self):
        r = self.run_case(sim.Scenario("buttons"))
        self.assertFalse(r.fake.light[-1])


class TestSdImages(Base):
    """The Welcome and Kody's pack the Kode installs from the station's microSD."""

    SHA_W = "a" * 64
    SHA_S = "b" * 64

    def run_images(self, files=None, install="installed", install_s=3.0, args=(), case="pass",
                   install_gap_s=0.0, run_refused=False, have=(), sha_refused=False, disarm_refused=False,
                   sd_rereads=None):
        sc = sim.Scenario(case, sd_files={"onboarding.bin": self.SHA_W, "storage.bin": self.SHA_S}
                          if files is None else files, install=install, install_s=install_s,
                          install_gap_s=install_gap_s, run_refused=run_refused, have=have,
                          sha_refused=sha_refused, disarm_refused=disarm_refused, sd_rereads=sd_rereads)
        r = Run(sc, args)
        self.addCleanup(r.cleanup)
        cfg = json.loads(r.cfg.read_text())
        images = {"welcome": {"file": "onboarding.bin", "sha256": self.SHA_W},
                  "storage": {"file": "storage.bin", "sha256": self.SHA_S}}
        cfg["sd_images"] = images
        r.cfg.write_text(json.dumps(cfg))
        return r.go()

    def run_without_images(self, args=()):
        r = Run(sim.Scenario("pass"), args)
        self.addCleanup(r.cleanup)
        cfg = json.loads(r.cfg.read_text())
        cfg["sd_images"] = None
        r.cfg.write_text(json.dumps(cfg))
        return r.go()

    def test_no_sd_images_key_installs_nothing_on_the_bench(self):
        r = self.run_without_images(["--no-place"])
        self.assertEqual(r.sent("file_sha") + r.sent("test_dot_welcome") + r.sent("test_dot_storage"), [])
        self.assertNotIn("welcome", r.results()["steps"])
        self.assertEqual(r.results()["verdict"], "BENCH PASS")

    def test_a_factory_run_without_both_images_or_the_kodeos_is_refused(self):
        for what in ("sd_images", "welcome", "storage", "expect"):
            with self.subTest(what=what):
                r = Run(sim.Scenario("pass"))
                self.addCleanup(r.cleanup)
                cfg = json.loads(r.cfg.read_text())
                if what == "sd_images":
                    cfg["sd_images"] = None
                elif what == "expect":
                    cfg["expect"] = {"kodeos": None}
                else:
                    del cfg["sd_images"][what]
                r.cfg.write_text(json.dumps(cfg))
                r.go()
                self.assertEqual(r.rc, final.EXIT_NO_REPORT, r.stdout[-2000:])
                self.assertEqual(r.fake.lines, [])          # the Kode is not even spoken to
                self.assertEqual(r.folders(), [])
                self.assertIn("a factory run is refused", r.stdout)

    def test_a_welcome_already_on_the_kode_is_not_written_again(self):
        r = self.run_images(have=("welcome",))
        res = r.results()
        self.assertEqual((r.rc, res["verdict"]), (0, "PASS"), r.stdout[-2000:])
        self.assertEqual(res["steps"]["welcome"]["status"], "PASS")
        self.assertIn("already on the Kode", res["steps"]["welcome"]["measured"])
        self.assertEqual(res["steps"]["storage"]["status"], "PASS")
        self.assertIn("installed from storage.bin", res["steps"]["storage"]["measured"])
        self.assertEqual(res["blocks"]["welcome_install"], 0.0)
        self.assert_grammar(r)

    def test_a_welcome_with_another_digest_on_the_kode_is_written(self):
        r = self.run_images(have=("storage",))
        res = r.results()
        self.assertEqual((r.rc, res["verdict"]), (0, "PASS"), r.stdout[-2000:])
        self.assertIn("installed from onboarding.bin", res["steps"]["welcome"]["measured"])
        self.assertNotIn("already on the Kode", res["steps"]["welcome"]["measured"])
        self.assertIn("welcome_install", res["blocks"])   # timed from its records: the sim runs them at speed
        self.assertEqual(len(r.sent("test_dot_welcome")), 1)
        self.assertIn("already on the Kode", res["steps"]["storage"]["measured"])
        self.assertEqual(res["blocks"]["storage_install"], 0.0)
        self.assert_grammar(r)

    def test_a_kodeos_without_sha_installs_as_before_and_says_so(self):
        r = self.run_images(have=("welcome", "storage"), sha_refused=True)
        res = r.results()
        self.assertEqual((r.rc, res["verdict"]), (0, "PASS"), r.stdout[-2000:])
        self.assertIn("installed from onboarding.bin", res["steps"]["welcome"]["measured"])
        self.assertIn("installed from storage.bin", res["steps"]["storage"]["measured"])
        self.assertEqual(r.sent("test_dot_welcome"),
                         [f'test_dot_welcome name="onboarding.bin" sha="{self.SHA_W}" project="onboarding"',
                          'test_dot_welcome name="onboarding.bin"'])
        self.assertIn("welcome: the Kode did not take sha=", r.stdout)

    @staticmethod
    def value(step, name):
        return next(v["value"] for v in step["values"] if v["name"] == name)

    def test_a_chunk_read_again_off_the_microsd_is_on_the_install_row(self):
        r = self.run_images(install_s={"welcome": 8.0, "storage": 3.0}, sd_rereads={"welcome": 2})
        res = r.results()
        w, st = res["steps"]["welcome"], res["steps"]["storage"]
        self.assertEqual((w["status"], st["status"]), ("PASS", "PASS"), w)
        self.assertIn("microSD read again 2 times (chunk at 32768, 65536)", w["measured"])
        self.assertEqual(self.value(w, "sd_rereads"), 2)
        self.assertNotIn("read again", st["measured"])
        self.assertEqual(self.value(st, "sd_rereads"), 0)

    def test_an_install_without_rereads_says_nothing_of_them(self):
        w = self.run_images().results()["steps"]["welcome"]
        self.assertNotIn("read again", w["measured"])
        self.assertEqual(self.value(w, "sd_rereads"), 0)

    def test_matching_images_are_installed_and_the_pass_waits_for_them(self):
        r = self.run_images()
        res = r.results()
        self.assertEqual((r.rc, res["verdict"]), (0, "PASS"), r.stdout[-2000:])
        self.assertEqual(res["steps"]["welcome"]["status"], "PASS")
        self.assertEqual(res["steps"]["storage"]["status"], "PASS")
        self.assertEqual(r.sent("test_dot_welcome"),
                         [f'test_dot_welcome name="onboarding.bin" sha="{self.SHA_W}" project="onboarding"'])
        self.assertEqual(r.sent("test_dot_storage"), [f'test_dot_storage name="storage.bin" sha="{self.SHA_S}"'])
        self.assertEqual(r.sent("file_sha"), ['file_sha name="/sdcard/onboarding.bin"',
                                              'file_sha name="/sdcard/storage.bin"'])
        self.assertLess(r.fake.lines.index(f'test_dot_storage name="storage.bin" sha="{self.SHA_S}"'),
                        r.fake.lines.index("test component=all"))
        self.assertIn("welcome_install", res["blocks"])
        self.assertIn("storage_install", res["blocks"])
        self.assert_grammar(r)

    def test_a_slow_install_holds_the_rfid_write_and_not_the_nfc_one(self):
        r = self.run_images(install_s=30.0, args=["--events"])
        self.assertEqual(r.results()["verdict"], "PASS", r.stdout[-2000:])
        order = [e["id"] for e in r.events() if e["type"] == "result" and e["id"] in ("nfc", "rfid", "welcome", "storage")]
        self.assertLess(order.index("nfc"), order.index("welcome"))
        self.assertLess(order.index("welcome"), order.index("rfid"))

    def results_order(self, r):
        return [e["id"] for e in r.events() if e["type"] == "result" and e["id"] in ("rfid", "welcome", "storage")]

    def test_an_install_that_goes_on_past_forty_seconds_holds_the_rfid_write_until_it_ends(self):
        # Over a written apps region the Welcome takes ~23 s and Kody's pack starts
        # only then: the pack is still being written 40 s after the arming.
        r = self.run_images(install_s={"welcome": 39.0, "storage": 15.0}, args=["--events"])
        res = r.results()
        self.assertEqual(res["verdict"], "PASS", r.stdout[-2000:])
        order = self.results_order(r)
        self.assertLess(order.index("storage"), order.index("rfid"), order)
        self.assertGreater(res["blocks"]["storage_install"], 14.0)

    def test_the_next_install_started_in_a_later_poll_is_waited_for_too(self):
        r = self.run_images(install_s={"welcome": 39.8, "storage": 3.0}, install_gap_s=8.0, args=["--events"])
        self.assertEqual(r.results()["verdict"], "PASS", r.stdout[-2000:])
        order = self.results_order(r)
        self.assertLess(order.index("storage"), order.index("rfid"), order)

    def test_an_install_the_run_never_reached_is_not_waited_for(self):
        # The run stops before the camera's end, where the Kode starts the installs.
        r = self.run_images(run_refused=True)
        res = r.results()
        self.assertEqual(res["steps"]["welcome"]["status"], "TIMEOUT", res["steps"]["welcome"])
        self.assertIn("never started", res["steps"]["welcome"]["measured"])
        self.assertEqual(res["verdict"], "FAIL")
        self.assertLess(res["total_s"], 60.0, res["total_s"])

    def test_a_wrong_image_is_the_stations_fault_and_installs_nothing(self):
        r = self.run_images(files={"onboarding.bin": "c" * 64, "storage.bin": self.SHA_S})
        res = r.results()
        self.assertEqual(res["verdict"], "NO VERDICT")
        self.assertEqual(res["steps"]["welcome"]["status"], "NO VERDICT")
        self.assertIn("station: the station's microSD does not hold the right image", res["steps"]["welcome"]["measured"])
        # The Kode is asked what it holds first, so it is armed; the station takes
        # both arms back and the run that would start the installs never begins.
        self.assertEqual([ln for ln in r.fake.lines if ln.startswith("test component=all")], [])
        self.assertNotIn("test_dot_result verdict=pass", r.fake.lines)
        self.assertEqual(r.sent("test_dot_welcome")[-1], 'test_dot_welcome name=""')
        self.assertEqual(r.sent("test_dot_storage")[-1], 'test_dot_storage name=""')
        self.assertEqual(r.fake.armed, {})
        self.assert_grammar(r)

    def test_only_what_was_armed_is_taken_back(self):
        r = self.run_images(files={"onboarding.bin": self.SHA_W, "storage.bin": "c" * 64}, have=("welcome",))
        self.assertEqual(r.results()["verdict"], "NO VERDICT")
        self.assertEqual(r.sent("test_dot_welcome"),
                         [f'test_dot_welcome name="onboarding.bin" sha="{self.SHA_W}" project="onboarding"'])
        self.assertEqual(r.sent("test_dot_storage"),
                         [f'test_dot_storage name="storage.bin" sha="{self.SHA_S}"', 'test_dot_storage name=""'])
        self.assertEqual(r.fake.armed, {})

    def test_a_kodeos_without_the_empty_name_stays_armed_and_says_so(self):
        r = self.run_images(files={"onboarding.bin": "c" * 64, "storage.bin": self.SHA_S}, disarm_refused=True)
        self.assertEqual(r.results()["verdict"], "NO VERDICT")
        self.assertEqual([ln for ln in r.fake.lines if ln.startswith("test component=all")], [])
        self.assertIn("welcome: the Kode did not take the disarm", r.stdout)
        self.assertEqual(r.fake.armed, {"welcome": "onboarding.bin", "storage": "storage.bin"})

    def test_a_missing_image_is_the_stations_fault_too(self):
        r = self.run_images(files={"storage.bin": self.SHA_S})
        res = r.results()
        self.assertEqual((res["verdict"], res["steps"]["welcome"]["status"]), ("NO VERDICT", "NO VERDICT"))
        self.assertEqual([ln for ln in r.fake.lines if ln.startswith("test component=all")], [])
        self.assertEqual(r.fake.armed, {})

    def test_a_kode_that_holds_both_images_reads_nothing_off_the_microsd(self):
        r = self.run_images(have=("welcome", "storage"))
        self.assertEqual(r.sent("file_sha"), [])
        self.assertEqual(r.results()["verdict"], "PASS")

    def test_a_failed_or_refused_install_fails_the_kode(self):
        for word in ("failed", "refused"):
            with self.subTest(word=word):
                r = self.run_images(install=word)
                res = r.results()
                self.assertEqual(res["verdict"], "FAIL")
                self.assertEqual(res["steps"]["welcome"]["status"], "FAIL")
                self.assertIn(word, res["steps"]["welcome"]["measured"])
                self.assertNotIn("test_dot_result verdict=pass", r.fake.lines)

    def test_an_install_with_no_last_record_does_not_pass(self):
        r = self.run_images(install="silent")
        res = r.results()
        self.assertEqual((res["verdict"], res["steps"]["welcome"]["status"]), ("FAIL", "TIMEOUT"))
        # The Kode runs the installs one after the other: the pack behind a Welcome
        # that never ends never starts.
        self.assertEqual(res["steps"]["storage"]["status"], "TIMEOUT")
        self.assertIn("never started", res["steps"]["storage"]["measured"])


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
                self.assertNotIn("test_dot_result verdict=pass", r.fake.lines)
                self.assertEqual(r.sent("test_dot_lock"), [])
                self.assertIn("test_dot_end", r.fake.lines)
                self.assertFalse((r.out / "record_E5-01.csv").exists())
                if flag == "--no-write":
                    self.assertEqual(r.sent("nfc_write"), [])
                else:
                    self.assertEqual(r.sent("test_dot_place"), [])


class TestExtraLimits(Base):
    """The limits the Kode does not judge (rondas/r5-qa-escapes.md): worked out on
    the PC, written on the row with their margin, and judged only when the
    station's settings say so in judge_extra."""

    # Per limit: the row, a Kode under it, and the word it gets.
    UNDER = {
        "panel": ("cam_colour", dict(expratio="1.80"), "OVER LIMIT"),
        "audio": ("audio", dict(tones=((1535, -7), (1957, -300))), "UNDER LIMIT"),
        "led": ("led", dict(led_flux=(5000, 73720, 104989)), "UNDER LIMIT"),
        "haptic": ("haptic", dict(haptic_on=(30.0, 35.0, 300.0)), "UNDER LIMIT"),
    }

    def run_judging(self, scenario, judge=()):
        r = Run(scenario)
        self.addCleanup(r.cleanup)
        cfg = json.loads(r.cfg.read_text())
        cfg["judge_extra"] = {k: True for k in judge}
        r.cfg.write_text(json.dumps(cfg))
        return r.go()

    def test_a_good_kode_is_recorded_with_its_margins_and_passes(self):
        res = self.run_case(sim.Scenario()).results()
        self.assertEqual(res["verdict"], "PASS")
        self.assertEqual(sorted(res["extra_limits"]), ["audio", "haptic", "led", "panel"])
        for key, (row, _, _) in self.UNDER.items():
            lim = res["extra_limits"][key]
            self.assertIs(lim["ok"], True, lim)
            self.assertFalse(lim["judged"])
            self.assertIn(lim["text"], res["steps"][row]["measured"])
            self.assertTrue(lim["text"].endswith(": margin ok"), lim["text"])
        self.assertEqual(res["extra_limits"]["audio"]["value"], -2.8, "the Kode logs tenths of a dB")
        self.assertEqual(res["extra_limits"]["haptic"]["value"], 282.9, "the middle of the three on windows")
        self.assertEqual(res["extra_limits"]["panel"]["value"], 1.0)

    def test_under_a_limit_is_recorded_only_by_default(self):
        for key, (row, knobs, word) in self.UNDER.items():
            with self.subTest(limit=key):
                res = self.run_case(sim.Scenario(**knobs)).results()
                self.assertEqual(res["verdict"], "PASS", "recorded only: the verdict does not move")
                self.assertEqual(res["steps"][row]["status"], "PASS")
                self.assertIs(res["extra_limits"][key]["ok"], False)
                self.assertIn(f"{word}, recorded only", res["steps"][row]["measured"])

    def test_under_a_judged_limit_fails_the_kode(self):
        for key, (row, knobs, word) in self.UNDER.items():
            with self.subTest(limit=key):
                res = self.run_judging(sim.Scenario(**knobs), [key]).results()
                self.assertEqual(res["steps"][row]["status"], "FAIL", res["steps"][row]["measured"])
                self.assertIn(f"{word}, judged: FAIL", res["steps"][row]["measured"])
                self.assertEqual(res["verdict"], "FAIL")
                self.assertTrue(res["extra_limits"][key]["judged"])

    def test_a_judged_limit_passes_a_good_kode(self):
        res = self.run_judging(sim.Scenario(), list(self.UNDER)).results()
        self.assertEqual(res["verdict"], "PASS")

    def test_one_weak_on_window_of_three_does_not_fail_the_motor(self):
        res = self.run_judging(sim.Scenario(haptic_on=(5.0, 280.0, 281.0)), ["haptic"]).results()
        self.assertEqual(res["steps"]["haptic"]["status"], "PASS")
        self.assertEqual(res["extra_limits"]["haptic"]["value"], 280.0)

    def test_a_value_that_never_came_is_said_and_no_verdict_when_judged(self):
        res = self.run_case(sim.Scenario(kode_logs=False)).results()
        self.assertEqual(res["verdict"], "PASS")
        self.assertIn("not received, recorded only", res["steps"]["audio"]["measured"])
        self.assertIsNone(res["extra_limits"]["haptic"]["ok"])
        res = self.run_judging(sim.Scenario(kode_logs=False), ["audio"]).results()
        self.assertEqual(res["steps"]["audio"]["status"], "NO VERDICT")
        self.assertEqual(res["verdict"], "NO VERDICT")


class TestBenchBright(Base):
    """E5_BRIGHT: the bench's dim panel, told to the Kode after the port is open
    (opening it reboots the Kode), before the placement wakes the glass."""

    def run_bright(self, pct, scenario=None, args=("--no-write",)):
        with mock.patch.dict(os.environ, {final.BRIGHT_ENV: pct}):
            return self.run_case(scenario or sim.Scenario(), args)

    def test_the_glass_level_is_told_after_the_info_and_before_the_placement(self):
        r = self.run_bright("40")
        self.assertEqual(r.sent("test_dot_bright"), ["test_dot_bright pct=40"])
        ln = r.fake.lines
        self.assertLess(ln.index("test_dot_info"), ln.index("test_dot_bright pct=40"))
        self.assertLess(ln.index("test_dot_bright pct=40"), ln.index(r.sent("test_dot_place")[0]))
        self.assertEqual(r.fake.bright, 40)
        self.assertEqual(r.results()["bench_bright"], {"pct": 40, "ok": True})

    def test_without_it_nothing_is_told(self):
        r = self.run_case(sim.Scenario(), ["--no-write"])
        self.assertEqual(r.sent("test_dot_bright"), [])
        self.assertIsNone(r.results()["bench_bright"])

    def test_a_factory_run_refuses_it(self):
        r = self.run_bright("40", args=())
        self.assertEqual(r.rc, final.EXIT_NO_REPORT)
        self.assertEqual(r.sent("test_dot_bright"), [])
        self.assertEqual(r.sent("test_dot_place"), [])

    def test_a_kodeos_without_the_verb_or_a_level_out_of_range_runs_nothing(self):
        for pct, sc in (("40", sim.Scenario(has_bright=False)), ("5", sim.Scenario()), ("dim", sim.Scenario())):
            with self.subTest(pct=pct):
                r = self.run_bright(pct, sc)
                self.assertEqual(r.rc, final.EXIT_NO_REPORT)
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
