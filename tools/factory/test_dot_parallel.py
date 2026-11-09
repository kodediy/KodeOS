#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The PC's work beside the operator, against a scripted run, no port.

`test component=all` gives the line back at the camera's end; the rest of the
run comes through `test_dot_poll`, and while the operator presses and touches
the PC does its own part (pads, NFC, RFID, radio) - but only once the Kode's
lane of rows is free, and NFC and RFID only once their rows have arrived.
This holds test_dot_final.py's run loop to that.

    python3 tools/factory/test_dot_parallel.py
"""

import argparse
import contextlib
import io
import shutil
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))

if "serial" not in sys.modules:
    _serial = types.ModuleType("serial")
    _serial.Serial = None
    _serial.SerialException = type("SerialException", (OSError,), {})
    sys.modules["serial"] = _serial

import test_dot_final as final  # noqa: E402

ROWS = [">|test=sd result=pass ms=2100", ">|test=imu result=pass ms=600",
        ">|timing=camera photos_ms=4000 analysis_ms=5000 led_ms=1500 total_ms=10500"]
OPS = [">|test=buttons result=pass ms=8000", ">|test=tp result=pass ms=3000"]
LANE = [">|test=audio result=pass ms=1000", ">|test=haptic result=pass ms=600", ">|test=mem result=pass ms=1"]


class Clock:
    def __init__(self):
        self.now = 1000.0

    def monotonic(self):
        return self.now

    def time(self):
        return 1793260000.0 + self.now

    def sleep(self, s):
        self.now += s


class FakeLink:
    """The open line, then one scripted answer per `test_dot_poll`."""

    def __init__(self, stream, polls, clock):
        self.stream, self.polls, self.clock = list(stream), list(polls), clock
        self.sent, self.log = [], []
        self.board = types.SimpleNamespace(send_line=self.sent.append, drain=lambda s=0: None)

    def read_line(self, deadline):
        self.clock.now += 0.01
        return self.stream.pop(0) if self.stream else None

    def cmd(self, line, timeout=10.0):
        self.log.append(line)
        assert line == "test_dot_poll", line
        recs = self.polls.pop(0) if self.polls else ["poll state=done lane=idle"]
        return [x[2:] if x.startswith(">|") else x for x in recs]


class Parallel(unittest.TestCase):

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="test-dot-parallel-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.clock = Clock()
        p = mock.patch.object(final, "CLOCK", self.clock)
        p.start()
        self.addCleanup(p.stop)

    def station(self, link):
        a = argparse.Namespace(events=False, out=str(self.tmp), config=str(self.tmp / "station.json"),
                               operator_sim=False, no_write=False, no_place=False, port="-", discover=False,
                               no_open=True)
        st = final.Station(a)
        st.link, st.outdir = link, self.tmp
        st.info = dict(serial="S", mac="38:44:be:1c:1d:44", kodeos="-", c5="-", date="-")
        order = []
        self.order = order
        st.radio_block = lambda: order.append("radio")
        st.rfid_step = lambda: (order.append("rfid"), st.pending.pop("rfid"))
        st.nfc_step = lambda: (order.append("nfc"), st.pending.pop("nfc"))

        def pads(sleep):
            order.append("pads_start")
            sleep(0.0)
            order.append("pads_end")
        st.pads_block = pads
        return st

    def run_all(self, st):
        with contextlib.redirect_stdout(io.StringIO()):
            st.run_all()

    def test_background_waits_for_the_free_lane(self):
        stream = ROWS + [">|test=nfc result=pass ms=10", ">|test=rfid result=pass ms=10", ">ok"]
        polls = [["poll state=running lane=busy"] + LANE[:2],
                 LANE[2:] + ["poll state=running lane=idle"],
                 OPS[:1] + ["poll state=running lane=idle"],
                 OPS[1:] + ["timing=all total_ms=20000 cam_end_ms=9000", "poll state=done lane=idle"]]
        link = FakeLink(stream, polls, self.clock)
        st = self.station(link)
        self.run_all(st)
        self.assertEqual(link.sent, ["test component=all"])
        self.assertEqual(self.order[:1], ["pads_start"])          # nothing before lane=idle (after mem)
        self.assertEqual(link.log[:2], ["test_dot_poll", "test_dot_poll"])
        self.assertEqual(sorted(x for x in self.order if x in ("nfc", "rfid", "radio")), ["nfc", "radio", "rfid"])
        ids = {r["id"] for r in st.results}
        self.assertTrue({"buttons", "tp", "audio", "haptic", "mem", "sd", "imu"} <= ids)
        self.assertEqual(st.done_bg, {"pads", "nfc", "rfid", "radio"})
        self.assertIsNotNone(st.op_end_t)
        self.assertTrue(st.run_ok)

    def test_nfc_waits_for_its_row(self):
        # The lane is free before the NFC row has come: the pads and the radio
        # go first, NFC once its row is there.
        stream = ROWS + [">|test=rfid result=pass ms=10", ">ok"]
        polls = [LANE + ["poll state=running lane=idle"],
                 ["poll state=running lane=idle"],
                 ["poll state=running lane=idle"],
                 [">|test=nfc result=pass ms=4000"] + OPS + ["poll state=done lane=idle"]]
        link = FakeLink(stream, polls, self.clock)
        st = self.station(link)
        self.run_all(st)
        self.assertLess(self.order.index("radio"), self.order.index("nfc"))
        self.assertEqual(st.done_bg, {"pads", "nfc", "rfid", "radio"})

    def test_sd_abort_ends_the_run(self):
        stream = [">|test=sd result=fail ms=100", ">|test=all result=aborted why=sd_missing",
                  ">err code=4 msg=aborted"]
        link = FakeLink(stream, [], self.clock)
        st = self.station(link)
        self.run_all(st)
        self.assertEqual(self.order, [])
        self.assertEqual(link.log, [])                            # no poll after a refused run
        steps = {r["id"]: r for r in st.results}
        self.assertEqual(steps["sd"]["status"], "FAIL")
        self.assertEqual(steps["mem"]["status"], "TIMEOUT")
        self.assertIn("microSD not detected", steps["mem"]["measured"])
        self.assertFalse(st.run_ok)


if __name__ == "__main__":
    unittest.main(verbosity=2)
