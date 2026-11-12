#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kcli's payload conversation, checked against a scripted board, no cable.

`install` and `file-put` send a file down the console in one conversation: a
command line, the board's `>|ready chunk=<n>`, then the bytes in windows of
that size, each one closed by the board's `>|ack`, and a terminator at the end.
The wire has no flow control of its own - the device acknowledges USB at line
rate and drops what its ring cannot hold - so the host sending past a window,
or giving up too late or never, is exactly the failure that shows up as a
corrupt image on a bench and nowhere else. This holds the host side of that
conversation to the contract in https://docs.kode.diy/internals/console-protocol.

The board is a fake serial port that behaves like the real one where it
matters: it parses the command line, announces a window, counts every byte it
receives and acknowledges a full window only. Time is virtual. kcli waits on
`time.monotonic()` deadlines of 10 and 30 seconds, and a fake board that made
the tests sit those out would make a timeout case cost half a minute; here
every empty read advances a clock instead, so a board that never answers is
judged by the same code path in a few milliseconds.

pyserial is not needed to run this: kcli only touches `serial.Serial` and
`serial.SerialException`, and CI's python has neither the package nor a cable.

    python3 tools/tests/test_kcli.py
"""

import contextlib
import io
import os
import sys
import tempfile
import types
import unittest
import zlib
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# A stand-in for the module, installed before kcli is imported so its
# import-time check passes whether or not pyserial is on this machine. Every
# case replaces Serial with its own fake anyway.
_serial = types.ModuleType("serial")
_serial.Serial = None
_serial.SerialException = type("SerialException", (OSError,), {})
sys.modules["serial"] = _serial

import kcli  # noqa: E402


PORT = "/dev/fake-kode-dot"
SERIAL_TIMEOUT_S = 0.2       # what Board sets as the port's read timeout


class Clock:
    """Virtual time, advanced by a read that finds nothing and by sleep()."""

    def __init__(self):
        self.now = 1000.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeBoardSerial:
    """The pyserial object Board drives, with a kodeOS console behind it.

    The knobs describe how the board misbehaves; the defaults are a healthy
    board. Everything the host does is recorded for the cases to judge.
    """

    def __init__(self, clock, chunk=1024, ready_line=None, ignore_installs=0,
                 stall_after_acks=None, fail_at_ack=None, close_line=">ok",
                 alive=True, wakes_on_reopen=False):
        self.clock = clock
        self.chunk = chunk                  # None: ready says no window at all
        self.ready_line = ready_line        # an answer to use instead of ready
        self.ignore_installs = ignore_installs
        self.stall_after_acks = stall_after_acks
        self.fail_at_ack = fail_at_ack      # (n, line): the n-th ack is `line`
        self.close_line = close_line        # None: the board never closes
        self.alive = alive                  # does the console answer `ping`
        self.wakes_on_reopen = wakes_on_reopen

        self.timeout = None
        self.port = None
        self.baudrate = None
        self.dtr = None
        self.rts = None
        self.is_open = False
        self.opens = 0

        self.rx = bytearray()               # what the board has said, unread
        self.lines = []                     # every command line received
        self.received = bytearray()         # every payload byte received
        self.size = 0
        self.in_payload = False
        self.outstanding = 0                # bytes since the last ack
        self.acks = 0
        self.stalled = False
        self.overruns = []                  # (bytes so far) at each breach
        self.widest_write = 0
        self._line = bytearray()

    # -- the pyserial surface Board uses --------------------------------

    def open(self):
        self.opens += 1
        self.is_open = True
        # A reopen resets the board, which is what the host relies on.
        self.rx.clear()
        self.in_payload = False
        self._line.clear()
        if self.wakes_on_reopen and self.opens > 1:
            self.alive = True

    def close(self):
        self.is_open = False

    @property
    def in_waiting(self):
        return len(self.rx)

    def read(self, n=1):
        if not self.rx:
            self.clock.now += self.timeout or SERIAL_TIMEOUT_S
            return b""
        out = bytes(self.rx[:n])
        del self.rx[:n]
        return out

    def flush(self):
        pass

    def write(self, data):
        if self.in_payload:
            self._payload(bytes(data))
        else:
            self._line += data
            while b"\n" in self._line:
                raw, _, rest = bytes(self._line).partition(b"\n")
                self._line = bytearray(rest)
                self._command(raw.decode().strip())
        return len(data)

    # -- the board ------------------------------------------------------

    def _say(self, text):
        self.rx += text.encode() + b"\r\n"

    def _command(self, text):
        self.lines.append(text)
        if text == "ping":
            if self.alive:
                self._say("ping")                      # the echo
                self._say(">|pong")
                self._say(">ok")
        elif text.startswith(("install ", "file_put ")):
            if self.ignore_installs > 0:
                self.ignore_installs -= 1
                return
            if self.ready_line is not None:
                self._say(self.ready_line)
                return
            self.size = int(kcli.fields(text)["size"])
            self.in_payload = True
            self.outstanding = 0
            # The prompt and the erase sequence AO_Log draws before a record,
            # and a log line in between: neither may hide the ready.
            self._say("!I kaos.app: transfer armed")
            window = "" if self.chunk is None else f" chunk={self.chunk}"
            self.rx += b"kode> \r\x1b[K>|ready" + window.encode() + b"\r\n"

    def _payload(self, data):
        window = self.chunk if self.chunk is not None else 4096
        self.widest_write = max(self.widest_write, len(data))
        # An ack still sitting unread means the host is writing ahead of it.
        if b">|ack" in self.rx or self.outstanding + len(data) > window:
            self.overruns.append(len(self.received) + len(data))
        self.received += data
        self.outstanding += len(data)
        if self.stalled:
            return
        if self.outstanding >= window:
            self.outstanding -= window
            self.acks += 1
            if self.fail_at_ack is not None and self.acks == self.fail_at_ack[0]:
                self._say(self.fail_at_ack[1])
                self.in_payload = False
                return
            if self.stall_after_acks is not None and self.acks > self.stall_after_acks:
                self.stalled = True
                return
            self._say(">|ack")
        if len(self.received) >= self.size:
            self.in_payload = False
            if self.close_line is not None:
                self._say("!I kaos.app: verified")
                self._say(self.close_line)


def sample(n):
    """n bytes that are not a pattern a shifted or repeated chunk could hide in."""
    return bytes(((i * 7) + (i // 251) + (i // 1024)) & 0xFF for i in range(n))


class PayloadConversation(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def local_file(self, data, name="image.bin"):
        path = os.path.join(self.tmp.name, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def talk(self, verb, data, dst="app", no_run=False, remote="/storage/n.txt",
             **board):
        """Run one kcli verb against a scripted board.

        @return (the fake port, the SystemExit the verb ended in or None,
                 what it printed)
        """
        clock = Clock()
        fake = FakeBoardSerial(clock, **board)
        path = self.local_file(data)
        out = io.StringIO()
        died = None
        with mock.patch.object(kcli, "time", types.SimpleNamespace(
                monotonic=clock.monotonic, sleep=clock.sleep)), \
                mock.patch.object(kcli.serial, "Serial", lambda: fake), \
                contextlib.redirect_stdout(out):
            port = kcli.Board(PORT)
            try:
                if verb == "install":
                    kcli.do_install(port, types.SimpleNamespace(
                        binfile=path, dst=dst, no_run=no_run, verbose=False))
                else:
                    kcli.do_file_put(port, types.SimpleNamespace(
                        local=path, remote=remote, verbose=False))
            except SystemExit as e:
                died = e
        return fake, died, out.getvalue()

    # -- the happy path ---------------------------------------------------

    def test_a_whole_install_sends_the_right_command_line(self):
        data = sample(2500)
        fake, died, _ = self.talk("install", data)
        self.assertIsNone(died)
        self.assertEqual(fake.lines, [
            "ping",
            f'install src="terminal" dst=app size=2500 crc32={zlib.crc32(data)} run=on',
        ])

    def test_an_app_install_names_the_project_its_image_carries(self):
        data = bytearray(sample(2500))
        data[0] = 0xE9                                  # a bare ESP image
        data[0x20:0x24] = (0xABCD5432).to_bytes(4, "little")
        data[0x50:0x70] = b"blink".ljust(32, b"\0")
        fake, died, _ = self.talk("install", bytes(data))
        self.assertIsNone(died)
        self.assertEqual(
            fake.lines[-1],
            f'install src="terminal" dst=app size=2500 '
            f'crc32={zlib.crc32(bytes(data))} run=on name="blink"')

    def test_an_install_without_run_asks_for_run_off(self):
        data = sample(100)
        fake, died, _ = self.talk("install", data, no_run=True)
        self.assertIsNone(died)
        self.assertEqual(
            fake.lines[-1],
            f'install src="terminal" dst=app size=100 crc32={zlib.crc32(data)} run=off')

    def test_an_install_to_another_destination_carries_no_run_word(self):
        data = sample(100)
        fake, died, _ = self.talk("install", data, dst="os")
        self.assertIsNone(died)
        self.assertEqual(
            fake.lines[-1],
            f'install src="terminal" dst=os size=100 crc32={zlib.crc32(data)}')

    def test_every_byte_of_the_file_arrives_in_order_and_ok_is_success(self):
        data = sample(2500)               # two full windows and a short tail
        fake, died, out = self.talk("install", data)
        self.assertIsNone(died)
        self.assertEqual(bytes(fake.received), data)
        self.assertEqual(fake.acks, 2)
        self.assertIn("installed and booting", out)

    def test_a_file_that_is_a_whole_number_of_windows_still_closes(self):
        data = sample(3 * 1024)           # the last window is full, so it is acked
        fake, died, _ = self.talk("install", data)
        self.assertIsNone(died)
        self.assertEqual(bytes(fake.received), data)
        self.assertEqual(fake.acks, 3)

    def test_a_file_shorter_than_one_window_needs_no_ack(self):
        data = sample(300)
        fake, died, _ = self.talk("install", data)
        self.assertIsNone(died)
        self.assertEqual(bytes(fake.received), data)
        self.assertEqual(fake.acks, 0)

    def test_file_put_quotes_the_remote_path_the_way_the_grammar_says(self):
        data = sample(10)
        fake, died, _ = self.talk("file_put", data, remote='/storage/my "n" notes.txt')
        self.assertIsNone(died)
        self.assertEqual(
            fake.lines[-1],
            'file_put path="/storage/my \\"n\\" notes.txt" '
            f"size=10 crc32={zlib.crc32(data)}")
        self.assertEqual(bytes(fake.received), data)

    # -- the window -------------------------------------------------------

    def test_the_host_never_sends_past_the_window_the_board_granted(self):
        for chunk in (256, 1024, 4000):
            with self.subTest(chunk=chunk):
                data = sample(5 * chunk + 17)
                fake, died, _ = self.talk("install", data, chunk=chunk)
                self.assertIsNone(died)
                self.assertEqual(fake.overruns, [])
                self.assertLessEqual(fake.widest_write, chunk)
                self.assertEqual(bytes(fake.received), data)
                self.assertEqual(fake.acks, 5)

    def test_a_ready_without_a_window_means_4096_bytes(self):
        data = sample(4096 + 4096 // 2)
        fake, died, _ = self.talk("install", data, chunk=None)
        self.assertIsNone(died)
        self.assertEqual(fake.overruns, [])
        self.assertEqual(fake.widest_write, 4096)
        self.assertEqual(fake.acks, 1)

    # -- the board says no ------------------------------------------------

    def test_an_error_in_place_of_ready_is_a_failure_with_the_boards_words(self):
        data = sample(500)
        fake, died, _ = self.talk("install", data,
                                  ready_line=">err code=4 msg=busy")
        self.assertIsNotNone(died)
        self.assertIn("install: >err code=4 msg=busy", str(died.code))
        self.assertEqual(bytes(fake.received), b"", "bytes went to a board that refused")

    def test_an_error_after_the_last_byte_is_a_failure_with_the_boards_words(self):
        data = sample(2500)
        fake, died, out = self.talk("install", data,
                                    close_line=">err code=7 msg=crc")
        self.assertIsNotNone(died)
        self.assertIn("install: >err code=7 msg=crc", str(died.code))
        self.assertNotIn("installed", out)

    def test_an_error_in_place_of_an_ack_stops_the_transfer(self):
        data = sample(10 * 1024)
        fake, died, _ = self.talk("install", data,
                                  fail_at_ack=(2, ">err code=9 msg=write"))
        self.assertIsNotNone(died)
        self.assertIn("install: >err code=9 msg=write", str(died.code))
        self.assertEqual(bytes(fake.received), data[:2 * 1024],
                         "the host kept sending after the board refused")

    def test_file_put_names_its_own_verb_in_the_failure(self):
        fake, died, _ = self.talk("file_put", sample(10),
                                  close_line=">err code=6 msg=full")
        self.assertIn("file_put: >err code=6 msg=full", str(died.code))

    # -- the board goes quiet --------------------------------------------

    def test_a_board_that_stops_acking_times_out_and_says_where(self):
        data = sample(10 * 1024)
        fake, died, _ = self.talk("install", data, stall_after_acks=3)
        self.assertIsNotNone(died)
        self.assertIn("no ack at", str(died.code))
        self.assertEqual(len(fake.received), 4 * 1024,
                         "the host sent past the window it was never acked for")

    def test_a_board_that_never_closes_times_out(self):
        fake, died, _ = self.talk("install", sample(2500), close_line=None)
        self.assertIsNotNone(died)
        self.assertIn("install: no terminator (timeout)", str(died.code))
        self.assertEqual(bytes(fake.received), sample(2500))

    def test_a_board_that_never_says_ready_is_given_one_reset_and_then_dropped(self):
        fake, died, out = self.talk("install", sample(500), ignore_installs=2)
        self.assertIsNotNone(died)
        self.assertIn("never said ready", str(died.code))
        self.assertEqual(fake.opens, 2, "one reopen, to reset the board, was the retry")
        self.assertEqual(sum(l.startswith("install ") for l in fake.lines), 2)
        self.assertEqual(bytes(fake.received), b"")

    def test_a_ready_lost_once_is_recovered_by_resetting_the_board(self):
        data = sample(2500)
        fake, died, out = self.talk("install", data, ignore_installs=1)
        self.assertIsNone(died)
        self.assertEqual(fake.opens, 2)
        self.assertEqual(sum(l.startswith("install ") for l in fake.lines), 2)
        self.assertEqual(bytes(fake.received), data)
        self.assertIn("no ready; resetting the board", out)

    def test_a_console_that_answers_only_after_a_reopen_is_used_after_it(self):
        data = sample(700)
        fake, died, out = self.talk("install", data, alive=False,
                                    wakes_on_reopen=True)
        self.assertIsNone(died)
        self.assertEqual(fake.opens, 2)
        self.assertEqual(bytes(fake.received), data)
        self.assertIn("no console", out)

    def test_a_console_that_never_answers_ping_sends_nothing(self):
        fake, died, _ = self.talk("install", sample(700), alive=False)
        self.assertIsNotNone(died)
        self.assertIn("ping (pre-flight)", str(died.code))
        self.assertFalse(any(l.startswith("install ") for l in fake.lines))
        self.assertEqual(bytes(fake.received), b"")


if __name__ == "__main__":
    unittest.main(verbosity=2)
