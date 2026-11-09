#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kble.py's two halves, checked against each other without a radio.

What is tested here is the seam between the half that holds the BLE link and
the half that runs one command: the hello, `busy`, `detach`, a bridge from
another version of this file, and that once the hello is over the bytes go
through untouched in both directions. The radio is a fake with the same four
verbs the bleak-backed one has, so none of this needs bleak, a bundle or a
board - `python3 tools/tests/test_kble.py`, exit 0 when every case held.

The sockets live in /tmp and not somewhere tidier, nor under $TMPDIR:
AF_UNIX paths are capped at 104 bytes on macOS, and most other directories
overrun that before the file name starts.
"""

import asyncio
import os
import shutil
import socket
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import kble  # noqa: E402


class FakeRadio:
    """A board that is always in range, unless told otherwise.

    `delay` stands in for the scan: the real one takes seconds, and the
    client that launched the bridge is attached by the time it ends. A fake
    that answered instantly would say "found" to nobody.
    """

    def __init__(self, found=True, delay=0.2, board="FAKE-DOT"):
        self.found = found
        self.board = board
        self.delay = delay
        self.opens = 0
        self.written = bytearray()
        self.closed = False
        self._rx = None
        self._drop = None
        self._loop = None

    async def open(self, say, timeout):
        self.opens += 1
        self._loop = asyncio.get_running_loop()
        await asyncio.sleep(self.delay)
        if not self.found:
            say("no board is advertising the console service")
            return False
        say(f"found {self.board} (00:00:00:00:00:00)")
        say("attached (fake radio)")
        return True

    def on_rx(self, cb):
        self._rx = cb

    def on_drop(self, cb):
        self._drop = cb

    async def write(self, data):
        self.written += data

    async def close(self):
        self.closed = True

    # -- what the test does on the board's behalf, from the test's thread --

    def board_says(self, data):
        self._loop.call_soon_threadsafe(self._rx, data)

    def drop(self):
        self._loop.call_soon_threadsafe(self._drop)


class Bridge:
    """kble.hold() on a loop of its own, the way the .app runs it."""

    def __init__(self, radio, path, version=kble.HELLO_VERSION, timeout=1.0):
        self.radio = radio
        self.path = path
        self.version = version
        self.timeout = timeout
        self.result = None
        self.errors = []          # what the loop could not hand to anybody
        self.thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        loop = asyncio.new_event_loop()
        loop.set_exception_handler(
            lambda _l, ctx: self.errors.append(
                ctx.get("exception") or ctx.get("message")))
        try:
            self.result = loop.run_until_complete(
                kble.hold(self.path, self.radio, self.timeout, self.version))
        finally:
            loop.close()

    def start(self):
        self.thread.start()
        until(lambda: os.path.exists(self.path), "the bridge never bound")

    def join(self):
        """The bridge's exit, and that nothing blew up quietly on the way.

        A handler that raises inside asyncio is reported to the loop's
        exception handler and to nobody else: the bridge would go on, with
        a traceback in bridge.log and the command none the wiser.
        """
        self.thread.join(timeout=3.0)
        if self.thread.is_alive():
            raise AssertionError("the bridge did not exit")
        if self.errors:
            raise AssertionError(f"unhandled inside the bridge: {self.errors}")
        return self.result


class StubbornBridge:
    """Answers `busy` to the first N hellos and `ok` after that.

    The real thing does this by accident: it frees a client's slot in the
    `finally` of the coroutine reading that client, so the slot outlives the
    command by however long the event loop takes to notice the EOF. Two
    commands in a row race that, and which one wins depends on the machine -
    which is no way to write a test. This produces the same sequence of
    answers by counting instead of by timing.

    Nothing here speaks the rest of the protocol: the case ends at the hello,
    because the hello is the whole subject.
    """

    def __init__(self, path, busy_first=1):
        self.path = path
        self.busy_first = busy_first
        self.hellos = 0
        self.attached = []        # sockets kept open, as a real bridge keeps them
        self.srv = socket.socket(socket.AF_UNIX)
        self.srv.bind(path)
        self.srv.listen(4)
        self.thread = threading.Thread(target=self._serve, daemon=True)

    def start(self):
        self.thread.start()

    def _serve(self):
        while True:
            try:
                client, _ = self.srv.accept()
            except OSError:                 # closed by close(); the case is over
                return
            line = b""
            while not line.endswith(b"\n"):
                c = client.recv(1)
                if not c:
                    break
                line += c
            self.hellos += 1
            answer = "busy" if self.hellos <= self.busy_first else "ok"
            client.sendall(f"kble {kble.HELLO_VERSION} {answer}\n".encode())
            if answer == "ok":
                self.attached.append(client)
            else:
                client.close()

    def close(self):
        self.srv.close()
        for client in self.attached:
            client.close()


def until(pred, what, timeout=2.0):
    end = time.time() + timeout
    while time.time() < end:
        if pred():
            return
        time.sleep(0.01)
    raise AssertionError(what)


def no_launch():
    raise AssertionError("the client launched a bridge it should have found")


class SerialTests(unittest.TestCase):
    """Choosing the board: what is read off the lines, and what is refused."""

    def test_the_board_is_read_from_a_fresh_scan_and_from_a_held_link(self):
        self.assertEqual(
            kble.board_of(["\x00kble: found KODE1TEST00001D (AA:BB)", "x"]),
            "KODE1TEST00001D")
        self.assertEqual(
            kble.board_of(["\x00kble: attached (link held since 10:00:00) "
                           "board=KODE1TEST00001D"]), "KODE1TEST00001D")
        self.assertIsNone(kble.board_of(["\x00kble: attached (link held "
                                         "since 10:00:00)"]))

    def test_no_serial_asks_nothing(self):
        self.assertIsNone(kble.wrong_board(["\x00kble: found OTHER (a)"], None))

    def test_the_wanted_board_is_accepted_whatever_its_case(self):
        self.assertIsNone(kble.wrong_board(
            ["\x00kble: found kode1test00001d (a)"], "KODE1TEST00001D"))

    def test_another_board_is_refused_and_the_message_names_both(self):
        why = kble.wrong_board(["\x00kble: found KODE78YR5EEV0KK (a)"],
                               "KODE1TEST00001D")
        self.assertIn("KODE78YR5EEV0KK", why)
        self.assertIn("KODE1TEST00001D", why)

    def test_a_bridge_that_does_not_say_which_board_is_refused(self):
        self.assertIsNotNone(kble.wrong_board(
            ["\x00kble: attached (link held since 10:00:00)"],
            "KODE1TEST00001D"))

    def test_serial_is_normalised(self):
        self.assertEqual(kble.norm_serial(" kode1test00001d\n"),
                         "KODE1TEST00001D")
        self.assertIsNone(kble.norm_serial("  "))
        self.assertIsNone(kble.norm_serial(None))

    def test_the_advert_name_beats_the_cached_one(self):
        class D:
            name = "KODE-DOT"

        class A:
            local_name = "KODE1TEST00001D"

        self.assertEqual(kble.advert_name(D, A), "KODE1TEST00001D")
        A.local_name = None
        self.assertEqual(kble.advert_name(D, A), "KODE-DOT")

    def test_the_bridge_is_launched_with_the_serial(self):
        self.assertEqual(kble.serial_args("KODE1TEST00001D"),
                         ["--serial", "KODE1TEST00001D"])
        self.assertEqual(kble.serial_args(None), [])


class HoldTests(unittest.TestCase):

    def setUp(self):
        # Under /tmp by name and not under $TMPDIR: on macOS TMPDIR is a long
        # per-user path that alone can push the socket past the 104-byte cap.
        self.tmp = tempfile.mkdtemp(prefix="kble-", dir="/tmp")
        self.path = os.path.join(self.tmp, "bridge.sock")
        self.links = []

    def tearDown(self):
        for link in self.links:
            link.close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def attach(self, launch=no_launch):
        link = kble.Link(2.0, sock_path=self.path, launch=launch)
        self.links.append(link)
        link.start()
        got, hit = link.wait_for(lambda l: "attached" in l, 2.0)
        self.assertIsNotNone(hit, f"never attached: {got}")
        return link, hit

    def detach(self):
        return kble.Link(2.0, sock_path=self.path, launch=no_launch).detach()

    def test_second_client_attaches_without_a_new_scan(self):
        radio = FakeRadio()
        bridge = Bridge(radio, self.path)
        bridge.start()

        a, _ = self.attach()
        a.send("ping")
        until(lambda: bytes(radio.written) == b"ping\n", "ping never reached the radio")
        radio.board_says(b">ok\n")
        _, hit = a.wait_for(lambda l: l == ">ok", 2.0)
        self.assertEqual(hit, ">ok")
        a.close()

        b, status = self.attach()
        self.assertIn("held since", status)
        self.assertIn("board=FAKE-DOT", status)
        self.assertEqual(radio.opens, 1, "the second client made the bridge scan again")
        b.send("ps")
        until(lambda: bytes(radio.written) == b"ping\nps\n", "ps never reached the radio")
        radio.board_says(b">|x\n>ok\n")
        got, hit = b.wait_for(lambda l: l == ">ok", 2.0)
        self.assertEqual(got, [">|x", ">ok"])
        b.close()

        self.assertTrue(self.detach())
        self.assertEqual(bridge.join(), "detach")
        self.assertTrue(radio.closed)
        self.assertFalse(os.path.exists(self.path))

    def test_a_second_client_while_one_is_attached_is_told_busy(self):
        radio = FakeRadio()
        bridge = Bridge(radio, self.path)
        bridge.start()
        a, _ = self.attach()

        b = kble.Link(2.0, sock_path=self.path, launch=no_launch)
        with self.assertRaises(SystemExit) as cm:
            b.start()
        self.assertIn("busy", str(cm.exception))

        a.send("ping")                     # the first client is unharmed
        until(lambda: bytes(radio.written) == b"ping\n", "ping never reached the radio")
        a.close()
        self.detach()
        bridge.join()

    def test_detach_with_no_bridge_says_so(self):
        self.assertFalse(self.detach())

    def test_bridge_exits_when_the_board_drops_the_link(self):
        radio = FakeRadio()
        bridge = Bridge(radio, self.path)
        bridge.start()
        a, _ = self.attach()

        radio.drop()
        self.assertEqual(bridge.join(), "dropped")
        until(lambda: not a.alive, "the attached client never saw the drop")
        self.assertFalse(os.path.exists(self.path))

    def test_bridge_exits_when_no_board_is_advertising(self):
        radio = FakeRadio(found=False)
        bridge = Bridge(radio, self.path)
        bridge.start()

        a = kble.Link(2.0, sock_path=self.path, launch=no_launch)
        self.links.append(a)
        a.start()
        _, hit = a.wait_for(lambda l: "no board" in l, 2.0)
        self.assertIsNotNone(hit)
        self.assertEqual(bridge.join(), "no board")
        until(lambda: not a.alive, "the client was not told the bridge left")
        self.assertFalse(os.path.exists(self.path))

    def test_client_launches_a_bridge_when_none_answers(self):
        with open(self.path, "w"):           # a socket file nobody serves
            pass
        radio = FakeRadio()
        launched = []

        def launch():
            launched.append(1)
            Bridge(radio, self.path).start()

        a, status = self.attach(launch=launch)
        self.assertEqual(launched, [1])
        self.assertNotIn("held since", status)   # this client saw the scan
        a.close()
        self.detach()

    def test_client_replaces_a_bridge_of_another_version(self):
        old_radio = FakeRadio()
        old = Bridge(old_radio, self.path, version=kble.HELLO_VERSION + 1)
        old.start()
        new_radio = FakeRadio()
        launched = []

        def launch():
            launched.append(1)
            Bridge(new_radio, self.path).start()

        a, _ = self.attach(launch=launch)
        self.assertEqual(old.join(), "stale")
        self.assertTrue(old_radio.closed)
        self.assertEqual(launched, [1])
        a.send("ping")
        until(lambda: bytes(new_radio.written) == b"ping\n", "ping went to the wrong bridge")
        a.close()
        self.detach()

    def test_payload_bytes_pass_through_verbatim(self):
        radio = FakeRadio()
        bridge = Bridge(radio, self.path)
        bridge.start()
        a, _ = self.attach()

        image = bytes(range(256)) * 16       # every byte, NUL and newline included
        a.send_raw(image)
        until(lambda: len(radio.written) == len(image), "the payload never arrived whole")
        self.assertEqual(bytes(radio.written), image)
        a.close()
        self.detach()
        bridge.join()

    def test_output_with_nobody_attached_is_discarded(self):
        radio = FakeRadio()
        bridge = Bridge(radio, self.path)
        bridge.start()
        a, _ = self.attach()
        a.close()
        time.sleep(0.1)                       # let the bridge see the close
        radio.board_says(b"!stray log line\n")
        time.sleep(0.1)

        b, _ = self.attach()
        b.send("ping")
        radio.board_says(b">ok\n")
        got, hit = b.wait_for(lambda l: l == ">ok", 2.0)
        self.assertEqual(hit, ">ok")
        self.assertNotIn("!stray log line", got)
        b.close()
        self.detach()
        bridge.join()

    def test_a_line_arriving_as_the_client_hangs_up_is_survived(self):
        """The board's trailing log line lands on a socket whose peer has
        just gone: the bridge has not read the EOF yet, so the write is
        attempted and fails. That must cost nothing - not the bridge, not
        the next command."""
        radio = FakeRadio()
        bridge = Bridge(radio, self.path)
        bridge.start()
        a, _ = self.attach()
        a.close()
        radio.board_says(b"!late log line\n")     # no pause: the race is the point

        b, _ = self.attach()
        b.send("ping")
        radio.board_says(b">ok\n")
        _, hit = b.wait_for(lambda l: l == ">ok", 2.0)
        self.assertEqual(hit, ">ok")
        b.close()
        self.detach()
        bridge.join()

    def test_a_busy_that_is_only_the_last_command_being_reaped_is_waited_out(self):
        """A command that follows another closely enough gets `busy` from a
        bridge whose slot is about to free itself. Reporting that verbatim
        sends the operator after a stuck bridge that does not exist, so the
        client waits BUSY_GRACE_S for it to clear and only then believes it.
        The case above - a client that really is attached - still gets the
        refusal, because there the busy never clears."""
        bridge = StubbornBridge(self.path)
        bridge.start()
        self.addCleanup(bridge.close)

        link = kble.Link(2.0, sock_path=self.path, launch=no_launch)
        self.links.append(link)
        link.start()                        # must not die on the first answer
        self.assertEqual(bridge.hellos, 2,
                         "the busy must be retried, not launched around")


if __name__ == "__main__":
    unittest.main(verbosity=2)
