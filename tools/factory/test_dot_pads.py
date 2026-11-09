#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The rear pads block of test_dot_final.py against a fake Kode, no port.

The station's relays and ADC are reached through the Kode
(`test_dot_relays`, `test_dot_adc`, `test_dot_loop`, `test_dot_boost`,
`bat_info`). Each case gives the voltages by (relay byte, channel) and checks
the verdict of the four pads steps, the bytes that reached the relays, and
that the station is always left at rest with the boost off.

    python3 tools/factory/test_dot_pads.py
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

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))

if "serial" not in sys.modules:
    _serial = types.ModuleType("serial")
    _serial.Serial = None
    _serial.SerialException = type("SerialException", (OSError,), {})
    sys.modules["serial"] = _serial

import test_dot_final as final  # noqa: E402
from kpayload import fields  # noqa: E402

GOOD_LOOP = "loop ok=1 b9600=20/20 b115200=20/20 rev=20/20 ms=410 why=-"


def good_volt(byte, boost, ch):
    """Volts at the measured point (the ADC reads the pin, behind /2 but on A1)."""
    t = {(0xFF, 0): 4.80, (0xFF, 1): 1.67, (0xFF, 2): 4.89, (0xFF, 3): 4.92,
         (0xFB, 1): 1.68, (0xFB, 0): 4.77,
         (0xFE, 0): 4.65, (0xFE, 1): 1.67, (0xFE, 2): 0.01, (0xFE, 3): 4.88}
    if byte == 0xFC:
        return {0: 4.64 if boost else 0.0, 1: 1.63, 2: 0.0, 3: 5.03}[ch]
    return t[(byte, ch)]


def good_bat(byte, boost):
    if byte == 0xFE:
        return "vbat_mv=4000 vbus_mv=4550 vbus=on boost=off current_ma=514 soc=80 charging=1 fault=0"
    if byte == 0xFC:
        return (f"vbat_mv=3900 vbus_mv=0 vbus=off boost={'on' if boost else 'off'} current_ma=-1150 "
                "soc=80 charging=0 fault=0")
    return "vbat_mv=4000 vbus_mv=4900 vbus=on boost=off current_ma=516 soc=80 charging=1 fault=0"


class FakeLink:
    """The station verbs of a Kode with the station's relays and ADC behind it."""

    def __init__(self, volt, bat, relays_ok=True, fail_on=None, loop=GOOD_LOOP):
        self.volt, self.bat, self.relays_ok, self.fail_on, self.loop = volt, bat, relays_ok, fail_on, loop
        self.byte, self.boost, self.log, self.writes = 0xFF, False, [], []

    def err(self, line, term):
        raise final.CommandError(line, term)

    def cmd(self, line, timeout=10.0):
        self.log.append(line)
        verb, _, rest = line.partition(" ")
        a = fields(rest)
        if verb == "test_dot_loop":
            if self.loop is None:
                self.err(line, ">err code=1 msg=test_dot_loop")
            assert self.byte == 0xFF, "the loop is asked with the relays at rest"
            return [self.loop]
        if verb == "test_dot_boost":
            self.boost = a["state"] == "on"
            return [f"boost on={1 if self.boost else 0} fault=0x00 current_ma={-1150 if self.boost else -20}"]
        if verb == "bat_info":
            if self.fail_on == "bat_info_delivery" and self.boost:
                self.err(line, None)
            return [self.bat(self.byte, self.boost)]
        if verb == "test_dot_relays":
            if not self.relays_ok:
                self.err(line, ">err code=4 msg=station_i2c")
            b = int(a["state"], 16)
            assert (b & 3) != 1, "a forbidden byte reached the relays"
            if self.byte == 0xFC:
                assert b in (0xFC, 0xFE), "left 0xFC without 0xFE"
            self.byte = b
            self.writes.append(b)
            return [f"relays state=0x{b:02X} ok=1"]
        if verb == "test_dot_adc":
            ch = int(a["ch"])
            v = self.volt(self.byte, self.boost, ch)
            return [f"adc ch={ch} mv={int(round(v * 1000 / (1 if ch == 1 else 2)))}"]
        raise AssertionError(f"unexpected line {line}")


class Pads(unittest.TestCase):

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="test-dot-pads-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def run_block(self, link):
        a = argparse.Namespace(events=False, out=str(self.tmp), config=str(self.tmp / "station.json"),
                               operator_sim=False, no_write=False, no_place=False, port="-", discover=False,
                               no_open=True)
        st = final.Station(a)
        st.link, st.outdir = link, self.tmp
        with contextlib.redirect_stdout(io.StringIO()):
            st.pads_block(lambda s: None)
        return {r["id"]: r for r in st.results}

    @staticmethod
    def vals(r):
        return {v["name"]: v["value"] for v in r["values"]}

    def test_good_case(self):
        link = FakeLink(good_volt, good_bat)
        r = self.run_block(link)
        self.assertEqual({k: v["status"] for k, v in r.items()},
                         {"pads_gnd": "PASS", "pads_charge": "PASS", "pads_delivery": "PASS", "pads_loop": "PASS"})
        self.assertEqual(self.vals(r["pads_loop"])["b115200"], "20/20")
        self.assertEqual(self.vals(r["pads_charge"])["current_ma"], 514)
        self.assertAlmostEqual(self.vals(r["pads_delivery"])["a0_v"], 4.64, 2)
        self.assertEqual(link.byte, 0xFF)
        self.assertFalse(link.boost)
        self.assertEqual(link.writes, [0xFF, 0xFB, 0xFF, 0xFE, 0xFC, 0xFE, 0xFF])   # the loop between 0xFF and 0xFE

    def test_pads_without_contact(self):
        def volt(byte, boost, ch):
            if ch == 0:
                return 0.0
            if ch == 1:
                return 3.31 if byte == 0xFB else 1.67
            return good_volt(0xFF, False, ch)
        link = FakeLink(volt, good_bat)
        r = self.run_block(link)
        self.assertEqual(r["pads_gnd"]["status"], "FAIL")
        self.assertIn("GND pogo pin without contact", r["pads_gnd"]["measured"])
        for k in ("pads_charge", "pads_delivery"):
            self.assertEqual(r[k]["status"], "FAIL")
            self.assertIn("+5V pogo pin without contact (A0=0.0 V)", r[k]["measured"])
        self.assertEqual(link.byte, 0xFF)
        self.assertFalse(link.boost)
        self.assertNotIn(0xFC, link.writes)

    def test_relays_do_not_answer(self):
        r = self.run_block(FakeLink(good_volt, good_bat, relays_ok=False))
        self.assertEqual(len(r), 4)
        for v in r.values():
            self.assertEqual(v["status"], "NO VERDICT")
            self.assertIn("station: the relays or the ADC do not answer", v["measured"])

    def test_no_5v_from_the_pc(self):
        link = FakeLink(lambda b, boost, ch: 3.0 if ch == 3 else good_volt(b, boost, ch), good_bat)
        r = self.run_block(link)
        for v in r.values():
            self.assertEqual(v["status"], "NO VERDICT")
            self.assertIn("no 5 V from the PC", v["measured"])
        self.assertEqual(link.byte, 0xFF)

    def test_sunk_charge_cuts(self):
        def volt(byte, boost, ch):
            return 3.5 if (byte == 0xFE and ch == 0) else good_volt(byte, boost, ch)
        link = FakeLink(volt, good_bat)
        r = self.run_block(link)
        self.assertEqual(r["pads_charge"]["status"], "FAIL")
        self.assertEqual(r["pads_delivery"]["status"], "NOT RUN")
        self.assertNotIn(0xFC, link.writes)
        self.assertEqual(link.byte, 0xFF)

    def test_exception_during_delivery(self):
        link = FakeLink(good_volt, good_bat, fail_on="bat_info_delivery")
        r = self.run_block(link)
        self.assertEqual(r["pads_delivery"]["status"], "NO VERDICT")
        self.assertIn("bat_info", r["pads_delivery"]["measured"])
        self.assertEqual(r["pads_gnd"]["status"], "PASS")
        self.assertEqual(r["pads_charge"]["status"], "PASS")
        # At the end: the boost off, then 0xFE and 0xFF, in that order.
        i_off = max(i for i, c in enumerate(link.log) if c == "test_dot_boost state=off")
        self.assertGreater(i_off, link.log.index("test_dot_boost state=on"))
        self.assertLess(i_off, max(i for i, c in enumerate(link.log) if c == "test_dot_relays state=0xFE"))
        self.assertEqual(link.writes[-3:], [0xFC, 0xFE, 0xFF])
        self.assertFalse(link.boost)
        self.assertEqual(link.byte, 0xFF)

    def test_loop_without_echo(self):
        link = FakeLink(good_volt, good_bat, loop="loop ok=0 b9600=0/20 b115200=0/20 rev=0/20 ms=900 why=no_echo")
        r = self.run_block(link)
        self.assertEqual(r["pads_loop"]["status"], "FAIL")
        self.assertIn("why=no_echo", r["pads_loop"]["measured"])
        self.assertIn("b9600=0/20", r["pads_loop"]["measured"])
        self.assertEqual(r["pads_delivery"]["status"], "PASS")

    def test_loop_on_a_kodeos_without_it(self):
        r = self.run_block(FakeLink(good_volt, good_bat, loop=None))
        self.assertEqual(r["pads_loop"]["status"], "NOT RUN")
        self.assertIn("firmware without test_dot_loop", r["pads_loop"]["measured"])

    def test_loop_partly_corrupt(self):
        r = self.run_block(FakeLink(good_volt, good_bat,
                                    loop="loop ok=1 b9600=20/20 b115200=17/20 rev=20/20 ms=500 why=corrupt"))
        self.assertEqual(r["pads_loop"]["status"], "FAIL")
        self.assertIn("b115200=17/20", r["pads_loop"]["measured"])
        self.assertEqual(self.vals(r["pads_loop"])["b115200"], "17/20")

    def test_loop_notes_the_pogo_pins(self):
        def volt(byte, boost, ch):
            if ch == 0:
                return 0.0
            if ch == 1:
                return 3.31 if byte == 0xFB else 1.67
            return good_volt(0xFF, False, ch)
        r = self.run_block(FakeLink(volt, good_bat))
        self.assertEqual(r["pads_loop"]["status"], "PASS")
        self.assertIn("+5V pogo pin without contact", r["pads_loop"]["measured"])
        self.assertIn("GND pogo pin without contact", r["pads_loop"]["measured"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
