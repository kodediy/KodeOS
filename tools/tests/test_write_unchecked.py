#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""write_unchecked against a fake esptool: the order of the calls, the blocks.

The Mac running the tests has no esptool, so fake modules with the names the
script imports are put in sys.modules first. The fakes record every call in
one list, in one shape, and the read-back is a call that fails the test.

    python3 tools/tests/test_write_unchecked.py
"""

import contextlib
import io
import os
import random
import sys
import tempfile
import types
import unittest
import zlib

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(TOOLS, "release"))

CALLS = []
BLOCK_TIMEOUTS = []
STATE = {"fail_block_at": None, "flash_size": "32MB"}


class FatalError(Exception):
    """The fake of esptool's own error."""


class FakeEsp:
    """The loader's side: records each method as it is called."""

    FLASH_WRITE_SIZE = 0x4000
    IS_STUB = True

    def flash_set_parameters(self, size):
        if size is None:
            raise TypeError("required argument is not an integer")
        CALLS.append(("flash_set_parameters", (size,)))

    def flash_defl_begin(self, size, compsize, offset):
        CALLS.append(("flash_defl_begin", (size, compsize, offset)))

    def flash_defl_block(self, data, seq, timeout=None):
        if seq == STATE["fail_block_at"]:
            raise FatalError("Failed to write to target RAM")
        BLOCK_TIMEOUTS.append(timeout)
        CALLS.append(("flash_defl_block", (data, seq)))

    def flash_defl_finish(self, **kwargs):
        CALLS.append(("flash_defl_finish", kwargs))

    def flash_md5sum(self, *args):
        CALLS.append(("flash_md5sum", args))
        raise AssertionError("the image must not be read back")


def detect_chip(port, connect_mode=None):
    CALLS.append(("detect_chip", (port, connect_mode)))
    return FakeEsp()


def run_stub(esp):
    CALLS.append(("run_stub", ()))
    return esp


def attach_flash(esp):
    CALLS.append(("attach_flash", ()))


def detect_flash_size(esp):
    CALLS.append(("detect_flash_size", ()))
    return STATE["flash_size"]


def reset_chip(esp, mode):
    CALLS.append(("reset_chip", (mode,)))


def _install_fakes():
    esptool = types.ModuleType("esptool")
    cmds = types.ModuleType("esptool.cmds")
    loader = types.ModuleType("esptool.loader")
    util = types.ModuleType("esptool.util")
    cmds.detect_chip = detect_chip
    cmds.run_stub = run_stub
    cmds.attach_flash = attach_flash
    cmds.detect_flash_size = detect_flash_size
    cmds.reset_chip = reset_chip
    # The timeout is the size it was asked about, so a test reads it back.
    loader.timeout_per_mb = lambda per_mb, size: float(size)
    loader.ERASE_WRITE_TIMEOUT_PER_MB = 40
    util.flash_size_bytes = lambda name: None if name is None else 0x2000000
    util.FatalError = FatalError
    esptool.cmds, esptool.loader, esptool.util = cmds, loader, util
    sys.modules["esptool"] = esptool
    sys.modules["esptool.cmds"] = cmds
    sys.modules["esptool.loader"] = loader
    sys.modules["esptool.util"] = util


_install_fakes()
import write_unchecked  # noqa: E402


class WriteUncheckedTest(unittest.TestCase):

    def setUp(self):
        del CALLS[:]
        del BLOCK_TIMEOUTS[:]
        STATE["fail_block_at"] = None
        STATE["flash_size"] = "32MB"
        self.calls = CALLS
        self.status = None
        # Half random, half one repeated line: several blocks once packed.
        line = b"the same line of text, over and over\n"
        self.data = random.Random(7).randbytes(51200) + (line * 2000)[:51200]
        self.assertEqual(102400, len(self.data))
        handle, self.path = tempfile.mkstemp(suffix=".bin")
        with os.fdopen(handle, "wb") as f:
            f.write(self.data)
        self.addCleanup(os.remove, self.path)

    @property
    def fail_block_at(self):
        return STATE["fail_block_at"]

    @fail_block_at.setter
    def fail_block_at(self, value):
        STATE["fail_block_at"] = value

    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.status = write_unchecked.main(list(argv))
        self.out, self.err = out.getvalue(), err.getvalue()
        return self.status

    def run_main_out(self, *argv):
        self.run_main(*argv)
        return self.out

    def run_main_err(self, *argv):
        self.run_main(*argv)
        return self.err

    def args(self, name):
        return [c[1] for c in self.calls if c[0] == name][0]

    def kwargs(self, name):
        return self.args(name)

    def test_the_calls_come_in_order(self):
        self.assertEqual(0, self.run_main("/dev/fake", "0x20000", self.path))
        names = [c[0] for c in self.calls]
        blocks = names.count("flash_defl_block")
        self.assertGreater(blocks, 1)
        self.assertEqual(["detect_chip", "run_stub", "attach_flash", "detect_flash_size",
                          "flash_set_parameters", "flash_defl_begin"]
                         + ["flash_defl_block"] * blocks + ["flash_defl_finish", "reset_chip"], names)
        self.assertEqual(("/dev/fake", "no-reset"), self.args("detect_chip"))
        self.assertEqual(("hard-reset",), self.args("reset_chip"))

    def test_the_blocks_inflate_back_to_the_file(self):
        self.run_main("/dev/fake", "0x20000", self.path)
        sent = [c[1] for c in self.calls if c[0] == "flash_defl_block"]
        self.assertEqual(list(range(len(sent))), [seq for _, seq in sent])
        self.assertTrue(all(len(data) <= 0x4000 for data, _ in sent))
        packed = b"".join(data for data, _ in sent)
        self.assertEqual(self.data, zlib.decompress(packed))
        self.assertEqual((len(self.data), len(packed), 0x20000), self.args("flash_defl_begin"))

    def test_the_timeouts_count_the_erases_the_stub_runs_ahead(self):
        self.run_main("/dev/fake", "0x20000", self.path)
        sent = [c[1][0] for c in self.calls if c[0] == "flash_defl_block"]
        self.assertGreater(len(sent), 2)
        inflater = zlib.decompressobj()
        expected = [0.0]    # the first block: esptool's floor, a size of 0
        for data in sent:
            n = len(inflater.decompress(data))
            erases = -(-n // (32 * 1024)) + 1
            expected.append(float(n + erases * 64 * 1024))
        # Block k waits for what block k-1 inflated to; so does the finish.
        self.assertEqual(expected[:len(sent)], BLOCK_TIMEOUTS)
        self.assertEqual(expected[len(sent)], self.kwargs("flash_defl_finish")["timeout"])

    def test_nothing_is_read_back(self):
        self.run_main("/dev/fake", "0x20000", self.path)
        self.assertNotIn("flash_md5sum", [c[0] for c in self.calls])

    def test_no_reset_leaves_the_p4_in_the_stub(self):
        self.run_main("/dev/fake", "0x20000", self.path, "--no-reset")
        self.assertNotIn("reset_chip", [c[0] for c in self.calls])
        self.assertFalse(self.kwargs("flash_defl_finish")["reboot"])

    def test_it_says_it_did_not_read_back(self):
        out = self.run_main_out("/dev/fake", "0x20000", self.path)
        self.assertRegex(out, r"Wrote 102400 bytes at 0x00020000 in \d+\.\d s; "
                              r"not read back: the bootloader checks its SHA-256 at boot")

    def test_an_esptool_error_mid_write_exits_1_without_a_reset(self):
        self.fail_block_at = 2
        err = self.run_main_err("/dev/fake", "0x20000", self.path)
        self.assertEqual(1, self.status)
        self.assertIn("error: Failed to write to target RAM", err)
        self.assertNotIn("reset_chip", [c[0] for c in self.calls])

    def test_a_detected_size_is_set_in_bytes(self):
        self.run_main("/dev/fake", "0x20000", self.path)
        self.assertEqual((0x2000000,), self.args("flash_set_parameters"))

    def test_an_unknown_flash_size_is_left_alone(self):
        STATE["flash_size"] = None    # the stub's ID read on this chip is not in esptool's table
        self.assertEqual(0, self.run_main("/dev/fake", "0x20000", self.path))
        names = [c[0] for c in self.calls]
        self.assertNotIn("flash_set_parameters", names)
        self.assertEqual(["detect_chip", "run_stub", "attach_flash", "detect_flash_size",
                          "flash_defl_begin"], names[:5])
        self.assertEqual(["flash_defl_finish", "reset_chip"], names[-2:])

    def test_a_missing_file_touches_nothing(self):
        self.assertEqual(1, self.run_main("/dev/fake", "0x20000", self.path + ".gone"))
        self.assertEqual([], self.calls)


if __name__ == "__main__":
    unittest.main()
