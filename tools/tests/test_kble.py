#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kble.py's two halves, checked against each other without a radio.

What is tested here is the seam between the half that holds the BLE link and
the half that runs one command: the hello, `busy`, `detach`, a bridge from
another version of this file, and that once the hello is over the bytes go
through untouched in both directions. The radio is a fake with the same four
verbs the bleak-backed one has, so none of this needs bleak, a bundle or a
board - `python3 tools/tests/test_kble.py`, exit 0 when every case held.

The scan has a fake of its own, a stand-in for bleak that hears the adverts a
test lists, so that which board is found, and what the command then checks
the bridge holds, run the real BleakRadio.open().

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
import types
import unittest
from unittest import mock

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
        until(self._listening, "the bridge never listened")

    def _listening(self):
        """Whether the bridge takes a connection. Not whether its path is
        there: the path appears at bind, a moment before listen, and a client
        that dials in that moment is refused. A connection that says nothing
        is closed by the bridge and changes nothing in it."""
        probe = socket.socket(socket.AF_UNIX)
        try:
            probe.connect(self.path)
        except OSError:
            return False
        finally:
            probe.close()
        return True

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
        self.assertEqual(kble.norm_board(" kode1test00001d\n"),
                         "KODE1TEST00001D")
        self.assertEqual(kble.norm_board("KODE qa one "), "KODE QA ONE")
        self.assertIsNone(kble.norm_board("  "))
        self.assertIsNone(kble.norm_board(None))

    def test_a_name_with_blanks_is_read_whole(self):
        self.assertEqual(
            kble.board_of(["\x00kble: found KODE qa one (AA:BB)"]),
            "KODE qa one")
        self.assertEqual(
            kble.board_of(["\x00kble: attached (link held since 10:00:00) "
                           "board=KODE qa one"]), "KODE qa one")
        # A name can hold the characters the line is made of.
        self.assertEqual(
            kble.board_of(["\x00kble: found KODE (b) board=x (AA:BB)"]),
            "KODE (b) board=x")
        self.assertEqual(
            kble.board_of(["\x00kble: attached (link held since 10:00:00) "
                           "board=KODE (b) board=x"]), "KODE (b) board=x")

    def test_a_named_board_is_accepted_whatever_its_case_and_blanks(self):
        found = ["\x00kble: found KODE qa one  (a)"]
        self.assertIsNone(kble.wrong_board(found, "KODE QA ONE"))
        held = ["\x00kble: attached (link held since 10:00:00) "
                "board=KODE qa one "]
        self.assertIsNone(kble.wrong_board(held, "KODE QA ONE"))

    def test_another_named_board_is_refused(self):
        why = kble.wrong_board(["\x00kble: found KODE qa two (a)"],
                               "KODE QA ONE")
        self.assertIn("KODE qa two", why)
        self.assertIn("KODE QA ONE", why)

    def test_a_name_with_a_word_of_the_status_does_not_end_the_wait(self):
        found = "\x00kble: found KODE attached failed (AA:BB)"
        self.assertFalse(kble.settled(found))
        self.assertTrue(kble.settled("\x00kble: attached"))
        self.assertTrue(kble.settled("\x00kble: attached (link held since "
                                     "10:00:00) board=KODE x"))
        self.assertTrue(kble.settled("\x00kble: no board named x"))
        self.assertTrue(kble.settled("\x00kble: failed to attach: x"))
        self.assertFalse(kble.settled("!a log line"))

    def test_the_board_is_asked_for_by_serial_or_by_name_not_both(self):
        self.assertEqual(kble.wanted_board(" kode1x ", None, None),
                         ("KODE1X", None))
        self.assertEqual(kble.wanted_board(None, "KODE qa one ", None),
                         (None, "KODE qa one"))
        self.assertEqual(kble.wanted_board(None, None, "kode1x"),
                         ("KODE1X", None))
        self.assertEqual(kble.wanted_board(None, None, None), (None, None))
        # The name beats the environment's serial, which is only a default.
        self.assertEqual(kble.wanted_board(None, "KODE qa one", "KODE1X"),
                         (None, "KODE qa one"))
        with self.assertRaises(SystemExit) as cm:
            kble.wanted_board("KODE1X", "KODE qa one", None)
        self.assertIn("--serial", str(cm.exception))
        self.assertIn("--name", str(cm.exception))

    def test_the_advert_name_beats_the_cached_one(self):
        class D:
            name = "KODE-DOT"

        class A:
            local_name = "KODE1TEST00001D"

        self.assertEqual(kble.advert_name(D, A), "KODE1TEST00001D")
        A.local_name = None
        self.assertEqual(kble.advert_name(D, A), "KODE-DOT")

    def test_the_bridge_is_launched_with_the_board(self):
        self.assertEqual(kble.board_args("KODE1TEST00001D", None),
                         ["--serial", "KODE1TEST00001D"])
        self.assertEqual(kble.board_args(None, "KODE qa one"),
                         ["--name", "KODE qa one"])
        self.assertEqual(kble.board_args(None, None), [])


class ParserTests(unittest.TestCase):
    """The board's name and a subcommand's file name are two things."""

    def test_the_board_name_survives_a_subcommand_with_a_name_of_its_own(self):
        args = kble.parser().parse_args(["--name", "KODE qa one", "screenshot"])
        self.assertEqual("KODE qa one", args.board_name)
        self.assertEqual("shot.bmp", args.name)

    def test_a_screenshot_with_no_board_named_asks_for_no_board(self):
        args = kble.parser().parse_args(["screenshot"])
        self.assertIsNone(args.board_name)
        self.assertEqual((None, None),
                         kble.wanted_board(args.serial, args.board_name, None))

    def test_the_file_to_fetch_is_not_taken_for_the_board(self):
        args = kble.parser().parse_args(["--name", "KODE qa one",
                                         "file-get", "notes.txt"])
        self.assertEqual("KODE qa one", args.board_name)
        self.assertEqual("notes.txt", args.name)
        args = kble.parser().parse_args(["file-get", "notes.txt"])
        self.assertIsNone(args.board_name)

    def test_a_command_s_own_timeout_leaves_the_scan_s_alone(self):
        args = kble.parser().parse_args(["--timeout", "40", "cmd", "info"])
        self.assertEqual(40.0, args.timeout)
        self.assertEqual(15.0, args.cmd_timeout)
        args = kble.parser().parse_args(["cmd", "--cmd-timeout", "3", "info"])
        self.assertEqual(25.0, args.timeout)
        self.assertEqual(3.0, args.cmd_timeout)


class PairTests(unittest.TestCase):
    """The wait for a pairing that a person answers on the Mac."""

    def pair(self, refusals, write_s=0.0):
        """Run _pair() against a link that refuses the first `refusals`
        writes, each write taking `write_s`; the clock is this case's own,
        so a second of waiting takes no time here."""
        radio = kble.BleakRadio(None, None)
        clock, tries, said = [0.0], [], []

        class Client:
            async def write_gatt_char(self, _uuid, _data, response):
                tries.append(response)
                clock[0] += write_s
                if len(tries) <= refusals:
                    raise RuntimeError("Encryption is insufficient")

        async def no_sleep(seconds):
            clock[0] += seconds

        radio.client = Client()
        with mock.patch.object(kble.asyncio, "sleep", no_sleep), \
                mock.patch.object(kble, "_clock", lambda: clock[0]):
            ok = asyncio.run(radio._pair(said.append))
        return ok, len(tries), said, clock[0]

    def test_a_code_typed_twenty_seconds_in_is_waited_for(self):
        ok, tries, said, _ = self.pair(20)
        self.assertTrue(ok)
        self.assertEqual(21, tries)
        self.assertEqual([], said)

    def test_a_pairing_nobody_answers_ends_and_says_why(self):
        ok, _, said, took = self.pair(10 ** 6)
        self.assertFalse(ok)
        self.assertGreaterEqual(took, kble.PAIR_WAIT_S)
        self.assertLess(took, kble.PAIR_WAIT_S + 2.0)
        self.assertTrue(said[-1].startswith("failed to pair:"))

    def test_the_wait_is_time_not_tries_when_a_write_is_slow(self):
        # A write can sit for seconds while the Mac shows its dialog.
        ok, tries, _, took = self.pair(10 ** 6, write_s=10.0)
        self.assertFalse(ok)
        self.assertLess(took, kble.PAIR_WAIT_S + 11.0)
        self.assertLess(tries, 6)

    def test_the_wait_outlasts_the_time_the_board_gives_a_pairing(self):
        self.assertGreater(kble.PAIR_WAIT_S, kble.BOARD_PAIR_S)

    def test_the_client_waits_out_a_scan_and_then_a_pairing(self):
        self.assertGreaterEqual(kble.attach_wait(25.0), 25.0 + kble.PAIR_WAIT_S)


class BridgeCase(unittest.TestCase):
    """A socket path of its own, and the clients to close at the end."""

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


class HoldTests(BridgeCase):

    def test_a_bridge_bound_but_not_listening_yet_is_waited_for(self):
        # The socket's path appears at bind, a moment before listen; a client
        # that dials in between is refused, and with no_launch that fails the
        # case. Stretch that moment and the bridge must still be found.
        real_listen = socket.socket.listen
        slowed = []

        def slow_listen(sock, *args):
            slowed.append(sock)
            time.sleep(0.3)
            return real_listen(sock, *args)

        with mock.patch.object(socket.socket, "listen", slow_listen):
            bridge = Bridge(FakeRadio(), self.path)
            bridge.start()
        self.assertTrue(slowed, "the bridge never went through listen")
        a, _ = self.attach()
        a.close()
        self.detach()
        bridge.join()

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


SVC = kble.SVC_UUID
UNRELATED = "0000180f-0000-1000-8000-00805f9b34fb"


class FakeAir:
    """What a scan hears: one (address, cached name, local name, service)
    per advert. CoreBluetooth's cached name is what dev.name reads; the local
    name is what the advert itself carries, None for a hidden board."""

    def __init__(self, *adverts):
        self.adverts = adverts
        self.written = bytearray()
        self.connected = []

    def bleak(self):
        """The module that stands in for bleak: the scanner hands the filter
        each advert in turn and answers the first it accepts, after a pause
        for the scan the client that launched the bridge is waiting out."""
        air = self

        class Dev:
            def __init__(self, address, name):
                self.address = address
                self.name = name

        class Ad:
            def __init__(self, local_name, uuid):
                self.local_name = local_name
                self.service_uuids = [uuid]

        class BleakScanner:
            @staticmethod
            async def find_device_by_filter(match, timeout=None):
                await asyncio.sleep(0.2)
                for address, cached, local, uuid in air.adverts:
                    dev = Dev(address, cached)
                    if match(dev, Ad(local, uuid)):
                        return dev
                return None

        class BleakClient:
            mtu_size = 247
            is_connected = True

            def __init__(self, dev, disconnected_callback=None):
                self.dev = dev

            async def connect(self):
                air.connected.append(self.dev.address)

            async def start_notify(self, _uuid, _cb):
                pass

            async def write_gatt_char(self, _uuid, data, response=False):
                air.written += data

            async def disconnect(self):
                self.is_connected = False

        mod = types.ModuleType("bleak")
        mod.BleakScanner = BleakScanner
        mod.BleakClient = BleakClient
        return mod


def advert(address, local, cached="KODE-DOT", uuid=SVC):
    return (address, cached, local, uuid)


class ScanTests(BridgeCase):
    """Which board the scan takes, and what the command checks it was given."""

    def start(self, air, serial=None, name=None):
        patch = mock.patch.dict(sys.modules, {"bleak": air.bleak()})
        patch.start()
        self.addCleanup(patch.stop)
        self.bridge = Bridge(kble.BleakRadio(serial, name), self.path)
        self.bridge.start()

    def attach_lines(self):
        """What main() reads before it sends anything: the lines up to the
        one that settles the attach."""
        link = kble.Link(2.0, sock_path=self.path, launch=no_launch)
        self.links.append(link)
        link.start()
        got, hit = link.wait_for(kble.settled, 2.0)
        self.assertIsNotNone(hit, f"nothing settled: {got}")
        return link, got

    def finish(self):
        self.detach()
        return self.bridge.join()

    def test_a_renamed_board_is_found_by_its_name_and_taken_commands(self):
        air = FakeAir(advert("AA:01", "KODE qa one"))
        self.start(air, name="kode QA one")
        link, got = self.attach_lines()
        self.assertIn("\x00kble: found KODE qa one (AA:01)", got)
        self.assertIsNone(kble.wrong_board(got, "KODE QA ONE"))
        self.assertIsNotNone(kble.wrong_board(got, "KODE QA TWO"))
        link.send("ping")
        until(lambda: bytes(air.written) == b"ping\n", "ping never went out")
        link.close()

        # The next command finds the link held and is told the name whole.
        link, got = self.attach_lines()
        self.assertIsNone(kble.wrong_board(got, "KODE QA ONE"))
        self.assertIsNotNone(kble.wrong_board(got, "KODE QA TWO"))
        link.close()
        self.assertEqual(self.finish(), "detach")

    def test_serial_takes_the_advertised_name_blanks_and_all(self):
        air = FakeAir(advert("AA:01", "KODE qa one"))
        self.start(air, serial="KODE QA ONE")
        link, got = self.attach_lines()
        self.assertIsNone(kble.wrong_board(got, "KODE QA ONE"))
        link.close()
        self.finish()

    def test_a_blank_at_the_end_of_either_name_does_not_matter(self):
        air = FakeAir(advert("AA:01", "KODE qa one "))
        self.start(air, name="KODE qa one  ")
        link, got = self.attach_lines()
        self.assertIsNone(kble.wrong_board(got, "KODE QA ONE"))
        link.close()
        self.finish()

    def test_a_board_that_still_advertises_its_serial_is_found_by_it(self):
        air = FakeAir(advert("AA:01", "KODE9OTHER00002"),
                      advert("AA:02", "KODE1TEST00001D"))
        self.start(air, serial="KODE1TEST00001D")
        link, got = self.attach_lines()
        self.assertIn("\x00kble: found KODE1TEST00001D (AA:02)", got)
        self.assertIsNone(kble.wrong_board(got, "KODE1TEST00001D"))
        self.assertEqual(air.connected, ["AA:02"])
        link.close()
        self.finish()

    def test_two_boards_with_names_are_told_apart_by_them(self):
        air = FakeAir(advert("AA:01", "KODE qa one"),
                      advert("AA:02", "KODE qa two"),
                      advert("AA:03", "KODE9OTHER00002"))
        self.start(air, name="KODE qa two")
        link, got = self.attach_lines()
        self.assertIn("\x00kble: found KODE qa two (AA:02)", got)
        self.assertEqual(air.connected, ["AA:02"])
        link.close()
        self.finish()

    def test_no_name_asked_takes_the_first_board(self):
        air = FakeAir(advert("AA:01", "KODE qa one"),
                      advert("AA:02", "KODE qa two"))
        self.start(air)
        link, got = self.attach_lines()
        self.assertIn("\x00kble: found KODE qa one (AA:01)", got)
        link.close()
        self.finish()

    def test_a_name_with_words_of_the_status_still_waits_for_the_attach(self):
        air = FakeAir(advert("AA:01", "KODE attached failed"))
        self.start(air, name="KODE attached failed")
        link, got = self.attach_lines()
        self.assertEqual(got[-1], "\x00kble: attached")
        self.assertIsNone(kble.wrong_board(got, "KODE ATTACHED FAILED"))
        link.close()
        self.finish()

    def test_a_serial_nobody_advertises_names_the_boards_that_do(self):
        air = FakeAir(advert("AA:01", "KODE qa one"),
                      advert("AA:02", "KODE9OTHER00002"),
                      advert("AA:03", "NOT A KODE", uuid=UNRELATED))
        self.start(air, serial="KODE1TEST00001D")
        _, got = self.attach_lines()
        said = got[-1]
        self.assertTrue(said.startswith("\x00kble: no board"), said)
        self.assertIn("KODE1TEST00001D", said)
        self.assertIn('"KODE qa one"', said)
        self.assertIn('"KODE9OTHER00002"', said)
        self.assertNotIn("NOT A KODE", said)
        self.assertIn("--name", said)         # a renamed board is asked for so
        self.assertEqual(self.bridge.join(), "no board")
        self.assertEqual(air.connected, [])

    def test_a_name_nobody_advertises_names_the_boards_that_do(self):
        air = FakeAir(advert("AA:01", "KODE qa one"))
        self.start(air, name="KODE qa two")
        _, got = self.attach_lines()
        said = got[-1]
        self.assertTrue(said.startswith("\x00kble: no board"), said)
        self.assertIn("KODE qa two", said)
        self.assertIn('"KODE qa one"', said)
        self.assertNotIn("--name", said)      # it was
        self.assertEqual(self.bridge.join(), "no board")

    def test_a_scan_that_hears_no_board_says_so_as_it_always_did(self):
        air = FakeAir(advert("AA:03", "NOT A KODE", uuid=UNRELATED))
        self.start(air, serial="KODE1TEST00001D")
        _, got = self.attach_lines()
        self.assertIn("no board KODE1TEST00001D is advertising the console "
                      "service", got[-1])
        self.assertNotIn("advertised by", got[-1])
        self.assertEqual(self.bridge.join(), "no board")

    def test_a_hidden_board_is_found_by_the_name_macos_cached(self):
        air = FakeAir(advert("AA:01", None, cached="KODE1TEST00001D"))
        self.start(air, serial="KODE1TEST00001D")
        link, got = self.attach_lines()
        self.assertIsNone(kble.wrong_board(got, "KODE1TEST00001D"))
        link.close()
        self.finish()


if __name__ == "__main__":
    unittest.main(verbosity=2)
