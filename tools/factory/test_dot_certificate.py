#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The certificate before a PASS, against a scripted Kode, no port.

A station that issues certificates sets test_dot_final's `certify`; a Kode
that passed every row gets its certificate before the PASS reaches it (the
PASS runs its factory reset), and a Kode that gets none does not pass. This
holds test_dot_final.py's tell_kode to that.

    python3 tools/factory/test_dot_certificate.py
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
    """Answers every line `>ok`, keeping what was sent."""

    def __init__(self):
        self.sent = []

    def cmd(self, line, timeout=10.0):
        self.sent.append(line)
        return []


class Certificate(unittest.TestCase):

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="test-dot-certificate-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        p = mock.patch.object(final, "CLOCK", Clock())
        p.start()
        self.addCleanup(p.stop)

    def station(self, bench=False):
        a = argparse.Namespace(events=False, out=str(self.tmp), config=str(self.tmp / "station.json"),
                               operator_sim=bench, no_write=False, no_place=False, port="-", discover=False,
                               no_open=True)
        st = final.Station(a)
        st.link, st.outdir = FakeLink(), self.tmp
        st.info = dict(serial="S", mac="38:44:be:1c:1d:44", kodeos="-", c5="-", date="-")
        return st

    def tell(self, st, verdict, certify):
        with mock.patch.object(final, "certify", certify), contextlib.redirect_stdout(io.StringIO()):
            return st.tell_kode(verdict)

    def row(self, st, sid):
        return next((r for r in st.results if r["id"] == sid), None)

    def test_a_kode_that_passed_gets_its_certificate_before_its_pass(self):
        st = self.station()
        asked = []

        def certify(link, run):
            asked.append(list(link.sent))
            return "KODE1TEST00002B, new"
        self.assertEqual(final.PASS, self.tell(st, final.PASS, certify))
        self.assertEqual([[]], asked)                     # nothing told the Kode before it
        self.assertEqual(["test_dot_result verdict=pass"], st.link.sent)
        self.assertEqual(final.PASS, self.row(st, "certificate")["status"])
        self.assertEqual("KODE1TEST00002B, new", self.row(st, "certificate")["measured"])

    def test_a_kode_that_gets_no_certificate_does_not_pass(self):
        st = self.station()

        def certify(link, run):
            raise final.CertificateRefused("factory-link does not know the board")
        self.assertEqual(final.FAIL, self.tell(st, final.PASS, certify))
        self.assertEqual(["test_dot_result verdict=fail"], st.link.sent)
        self.assertEqual(final.FAIL, self.row(st, "certificate")["status"])
        self.assertIsNone(self.row(st, "factory_reset"))

    def test_a_kode_that_failed_is_asked_for_no_certificate(self):
        st = self.station()
        certify = mock.Mock()
        self.assertEqual(final.FAIL, self.tell(st, final.FAIL, certify))
        certify.assert_not_called()
        self.assertIsNone(self.row(st, "certificate"))

    def test_a_bench_run_is_asked_for_no_certificate(self):
        st = self.station(bench=True)
        st.screen = lambda *a: None
        certify = mock.Mock()
        self.assertEqual(final.PASS, self.tell(st, final.PASS, certify))
        certify.assert_not_called()

    def test_a_station_that_issues_none_passes_as_before(self):
        st = self.station()
        self.assertEqual(final.PASS, self.tell(st, final.PASS, None))
        self.assertIsNone(self.row(st, "certificate"))
        self.assertEqual(["test_dot_result verdict=pass"], st.link.sent)


if __name__ == "__main__":
    unittest.main()
