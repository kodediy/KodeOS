#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kble - the host side of the kodeOS console, over BLE instead of the cable.

The console protocol is the same one `kcli.py` speaks and
https://docs.kode.diy/internals/console-protocol documents: one line is one command,
every response line starts with `>`
(records `>|`, terminators `>ok` / `>err`), everything else (`!`) is a log
the host did not ask for. Only the pipe changes.

    kble.py term                       an interactive terminal
    kble.py cmd "app_list"             one command, then its terminator
    kble.py install path/to/app.bin
    kble.py screenshot -o shot.png     what is on the panel, over the air
    kble.py file-get notes.txt
    kble.py detach                     let go of the board

The first command scans for the board and attaches; every command after that
finds the link already up and attaches in milliseconds, because the bridge
below outlives the command that started it. That held link used to be a
necessity - an advert was consumed by the connection it attracted and the
board put out no other, so a bridge that died with its command needed a
fresh `ble_advertise state=on` on the cable each time. The firmware now
advertises for five minutes after a boot, a `ble_up` or a central with its bond
leaving, and then goes off until the next of those
(https://docs.kode.diy/internals/ble-console), so
holding the link is only an
optimisation: 0.18 s against 2.3 s. The board sees one connection, kept for
as long as this machine cares to: until `kble.py detach`, until the board
drops it (`ble_down`, a reset, out of range), or until Bluetooth goes away.
Never on idle - while it is held every log line is mirrored over the air,
which is what a session costs.

Pairing is required (KODEOS_BLE_CONSOLE_PAIRING) and asks nothing: it is
Just Works, so the first attach of a machine bonds in a few hundred
milliseconds with no passkey and no dialog. The bond survives a reboot on
both sides, so every later attach is silent too. See open() for the one
thing a Mac needs that a phone does not.

A machine that paired under a DIFFERENT build keeps its half of the bond,
and that half is in the way: it offers a key the board cannot match and
drops the link about half a second in. On the cable that reads `connected`
followed straight away by `disconnected (reason 531)` - 0x213, remote user
terminated - and here it reads `failed to attach: ... Peer removed pairing
information`. Measured 2026-08-24. Tell the machine to forget the board (on
macOS: `blueutil --unpair <addr>`, or System Settings > Bluetooth) and it
attaches like any other; `ble_unpair` on the cable clears the board's half.

Which board: with two Kodes in range the first one scanned wins, and a bond
that belongs to the other one answers "Insufficient Authentication" - which
reads as a broken bond and is not. `--serial KODE1TEST00001D`, or the
environment's KODE_SERIAL, names the one board to attach to: the scan skips
every other advert (the name is the serial, read from the advert itself, not
from the name CoreBluetooth caches per address), and a bridge already holding
a different board is refused instead of served. Without either, the first
Kode is taken, as before.

macOS needs one piece of scaffolding and it is worth explaining, because the
failure is otherwise baffling. CoreBluetooth refuses any process whose
Info.plist has no NSBluetoothAlwaysUsageDescription, and it refuses it by
aborting with SIGABRT and no message. No Python on a Mac declares that key,
so bleak cannot run from a terminal at all. This script therefore builds a
tiny signed .app under the cache that declares it, launches ITSELF inside
that bundle through launchd (`open -a`, which is what gives the bundle its
own TCC identity - invoking the executable directly inherits ours and aborts
just the same), and the two halves talk over a Unix socket next to it.

The half inside the bundle holds the link and is otherwise a dumb byte pipe.
It listens on the socket; a command connects, says hello (`kble 1 attach`),
and from there bytes cross verbatim in both directions until the command
hangs up. One command at a time - a second is told `busy`. Everything that
knows the protocol - the terminal, the stop-and-wait of install - stays
here, in one place, on this side of the socket. The number in the hello is
there for the bridge's longevity: the bundle runs a COPY of this file, taken
when it was launched, so a bridge from before an edit can still be holding
the link. One that hears a hello from another version steps aside - drops
the link, exits - and the command launches a fresh one.

Needs bleak. If it is missing this script builds a venv under the cache and
re-executes itself in it.
"""

import argparse
import asyncio
import os
import socket
import subprocess
import sys
import threading
import time
import traceback
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Everything the bundle touches lives OUTSIDE the repository, and that is not
# tidiness. An app launched by launchd gets a TCC identity of its own - which
# is the whole point - and that identity has no access to ~/Documents, where
# this checkout lives. It cannot read a venv there, and it cannot read this
# file either: the first attempt died on `Operation not permitted` reading
# its own pyvenv.cfg. So the interpreter, the bundle, a copy of this script
# and the socket all sit in the user's cache, where no permission is
# involved.
CACHE = os.path.expanduser("~/Library/Caches/kode-kble")
APP = os.path.join(CACHE, "KodeBle.app")
LOG = os.path.join(CACHE, "bridge.log")
BRIDGE_COPY = os.path.join(CACHE, "kble_bridge.py")
SOCK = os.path.join(CACHE, "bridge.sock")

SVC_UUID = "4b4f4445-0001-4f53-a2d1-7c0e9b5f3a61"
RX_UUID = "4b4f4445-0002-4f53-a2d1-7c0e9b5f3a61"
TX_UUID = "4b4f4445-0003-4f53-a2d1-7c0e9b5f3a61"

CHUNK = 4096          # what the board asks for in `>|ready chunk=`

# The hello between the two halves: `kble <version> attach|detach` one way,
# `kble <version> ok|busy|stale` back. Bump it when the wire changes.
HELLO_VERSION = 2

# The environment variable that names the board when --serial is not given.
SERIAL_ENV = "KODE_SERIAL"

# How long a `busy` is waited out before it is believed. The bridge frees its
# slot in the `finally` of the coroutine reading that client, which runs when
# its event loop notices the EOF - a moment AFTER the command that owned the
# socket has exited. So two commands run back to back race that reaping, and
# on a fast machine the second one loses it often enough to look like a stuck
# bridge, which is the one diagnosis the message invites and the one thing
# that is not wrong. A client that genuinely holds the link holds it for as
# long as its command runs, so a second of patience never hides one: it only
# ever turns a refusal that was about to clear into the connection it was
# always going to become.
BUSY_GRACE_S = 1.0


def die(msg):
    sys.exit(f"kble: {msg}")


def norm_serial(text):
    """A serial as the advert spells it: upper case, no surrounding blanks.
    None when nothing was asked for."""
    if text is None:
        return None
    text = text.strip().upper()
    return text or None


def advert_name(dev, ad):
    """The name this advert carries. The advert's own local name wins over
    dev.name: CoreBluetooth caches the latter per address, so it can still
    read `KODE-DOT` for a board that has long advertised its serial."""
    return ad.local_name or dev.name or ""


def board_of(lines):
    """The board a bridge reported, from the lines it said while attaching:
    `found NAME (addr)` on a fresh scan, `board=NAME` on a held link.
    None when it did not say (a bridge that predates the name)."""
    name = None
    for line in lines:
        line = line.lstrip("\x00")
        if line.startswith("kble: "):
            line = line[len("kble: "):]
        if line.startswith("found "):
            name = line.split()[1]
        for word in line.split():
            if word.startswith("board="):
                name = word[len("board="):]
    return name


def wrong_board(lines, serial):
    """Why this command must not use the link it was given, or None.

    The scan only ever finds `serial`, so a mismatch can only be a bridge that
    was already holding another board (or one too old to say which)."""
    if serial is None:
        return None
    held = board_of(lines)
    if held is None:
        return (f"the bridge did not say which board it holds, and {serial} "
                "was asked for: `kble.py detach` and try again")
    if held.upper() != serial:
        return (f"the bridge holds {held}, not {serial}: `kble.py detach` "
                "first")
    return None


def serial_args(serial):
    """The bridge's command line for the board it is to look for. The bundle
    is started by launchd and inherits no environment, so it goes as an
    argument."""
    if serial is None:
        return []
    return ["--serial", serial]


# ---- Getting a Python that can actually open a radio -----------------------

def launch_bridge(timeout, serial=None):
    """Start the half that holds the link, detached from this command.

    The macOS scaffolding this needs - a venv with bleak in it, and a signed
    .app that declares NSBluetoothAlwaysUsageDescription so CoreBluetooth
    will talk to it at all - lives in kblebundle.py. It used to live here as
    well, in a second copy: this file worked the mechanism out first, and
    when kmirror.py needed the same thing the machinery was lifted into a
    module while this file was left on its own copy for being already
    bench-verified. Two mechanisms doing one job is the shape this project
    calls a bug, and "it already works" is what every such pair says about
    itself, so the copy is gone and this calls the module like the other
    tool does. What stays deliberately NOT shared is the BLE session itself
    - kble's is one characteristic and line-oriented, kmirror's is three with
    a binary frame stream mixed in - see kblebundle.py's own docstring.

    Imported inside the function, not at module scope: the half of this file
    that runs INSIDE the bundle re-executes from a copy in the cache
    directory, where `tools/` is not on the path - the same reason kpayload
    is imported lazily further down. That half never reaches this function.

    The bundle identifier stays `diy.kode.kble`, because that is what macOS
    files its Bluetooth permission under: changing it would ask the user for
    the prompt again.
    """
    import kblebundle

    py = kblebundle.python_with_bleak(CACHE)
    if sys.platform == "darwin":
        kblebundle.ensure_bundle(
            app_dir=APP, exe_name="KodeBle", bundle_id="diy.kode.kble",
            display_name="Kode BLE console",
            usage_text="Talks to a Kode Dot's BLE console.",
            py=py, script_src=os.path.abspath(__file__),
            script_copy=BRIDGE_COPY, log_path=LOG)
        kblebundle.launch(APP, ["--hold", "--timeout", str(timeout)]
                          + serial_args(serial))
    else:
        os.makedirs(CACHE, exist_ok=True)
        with open(LOG, "ab") as log:
            subprocess.Popen([py, os.path.abspath(__file__),
                              "--hold", "--timeout", str(timeout)]
                             + serial_args(serial),
                             stdout=log, stderr=log, start_new_session=True)


# ---- The half inside the bundle: hold the link, pipe the bytes ------------

class BleakRadio:
    """The air, as the bridge sees it: scan, attach, bytes each way, gone.

    Four verbs and two callbacks, so that hold() can be run against a fake
    in tools/tests/test_kble.py. Both callbacks arrive on the event loop - bleak
    marshals CoreBluetooth's threads onto it - so hold() may touch its state
    from them without a lock.
    """

    def __init__(self, serial=None):
        self.serial = norm_serial(serial)
        self.board = None
        self.client = None
        self.window = 20
        self._rx = None
        self._drop = None

    def on_rx(self, cb):
        self._rx = cb

    def on_drop(self, cb):
        self._drop = cb

    async def open(self, say, timeout):
        from bleak import BleakClient, BleakScanner
        names = {}

        def wanted(d, ad):
            if SVC_UUID.lower() not in [u.lower() for u in ad.service_uuids]:
                return False
            name = advert_name(d, ad)
            names[d.address] = name
            return (self.serial is None) or (name.upper() == self.serial)

        dev = await BleakScanner.find_device_by_filter(wanted, timeout=timeout)
        if dev is None:
            if self.serial is None:
                say("no board is advertising the console service - "
                    "run `ble_up` on the cable, or check nothing else is "
                    "already attached")
            else:
                say(f"no board {self.serial} is advertising the console "
                    "service - run `ble_up` on the cable, or check nothing "
                    "else is already attached")
            return False
        self.board = names.get(dev.address) or dev.name or "?"
        say(f"found {self.board} ({dev.address})")
        self.client = BleakClient(dev, disconnected_callback=self._on_drop)
        await self.client.connect()
        try:
            await self.client.start_notify(TX_UUID, self._on_notify)
        except Exception:                     # noqa: BLE001 - any refusal
            # Refused for want of an encrypted link. Provoke the pairing and
            # come back; see _pair() for why subscribing cannot provoke it
            # itself. Tried in this order so a board with pairing off, and a
            # peer already bonded, both take the short path and write nothing.
            if not await self._pair(say):
                return False
            await self.client.start_notify(TX_UUID, self._on_notify)
        mtu = getattr(self.client, "mtu_size", 0) or 23
        self.window = max(mtu - 3, 20)
        say("attached")
        return True

    async def _pair(self, say):
        """Make CoreBluetooth pair before anything needs the encrypted link.

        Both characteristics require encryption, so SOMETHING has to provoke
        the pairing - and which something is not a free choice on a Mac.
        Subscribing looks like the natural trigger and is not: start_notify
        is a CCCD write, and CoreBluetooth hands a CCCD permission failure
        back to the application instead of pairing on it. Do that first and
        the attach dies with "Encryption is insufficient" having started
        nothing, while the board sits waiting out its 30-second security
        timeout. Measured on 2026-08-27, twice.

        A characteristic write WITH RESPONSE is what it does act on. The
        payload is a bare newline, which the board's line assembler turns
        into an empty command and discards. The first attempt is expected to
        fail - it is the trigger, not the message - so this retries while the
        OS runs the exchange underneath.

        Only reached when subscribing has already been refused, so nothing is
        written on a board whose console is open or a peer already bonded.
        """
        last = None
        for _ in range(6):
            try:
                await self.client.write_gatt_char(RX_UUID, b"\n", response=True)
                return True
            except Exception as exc:          # noqa: BLE001 - any refusal
                last = exc
                await asyncio.sleep(1.0)
        say(f"failed to pair: {last}")
        return False

    def _on_notify(self, _h, data):
        if self._rx is not None:
            self._rx(bytes(data))

    def _on_drop(self, _client):
        if self._drop is not None:
            self._drop()

    async def write(self, data):
        pending = bytes(data)
        while pending:
            take, pending = pending[:self.window], pending[self.window:]
            await self.client.write_gatt_char(RX_UUID, take, response=False)

    async def close(self):
        if self.client is not None and self.client.is_connected:
            await self.client.disconnect()


async def hold(sock_path, radio, timeout, version=HELLO_VERSION):
    """Hold the link and serve it to one command at a time.

    Binds the socket BEFORE scanning, so the command that launched this is
    attached while the scan runs and reads `found` / `attached` / `no board`
    as they happen - the same lines it read when the bridge was its own.
    Returns why it ended: detach, dropped, no board, failed, stale.
    """
    loop = asyncio.get_running_loop()
    done = loop.create_future()
    client = {"w": None}
    since = {"t": None}

    def end(why):
        if not done.done():
            done.set_result(why)

    def to_client(data):
        w = client["w"]
        if w is not None and not w.is_closing():
            w.write(data)

    def say(msg):
        to_client(("\x00kble: " + msg + "\n").encode())

    radio.on_rx(to_client)
    radio.on_drop(lambda: end("dropped"))

    async def serve(reader, writer):
        def answer(word):
            writer.write(f"kble {version} {word}\n".encode())

        hello = (await reader.readline()).decode("utf-8", "replace").split()
        if len(hello) != 3 or hello[0] != "kble":
            writer.close()
            return
        if hello[1] != str(version):
            # The command is the freshly-run script; this bridge is the copy
            # that predates it. Step aside and let it launch a current one.
            answer("stale")
            writer.close()
            end("stale")
            return
        if hello[2] == "detach":
            answer("ok")
            writer.close()
            end("detach")
            return
        if hello[2] != "attach" or client["w"] is not None:
            answer("busy")
            writer.close()
            return
        answer("ok")
        client["w"] = writer
        if since["t"] is not None:
            board = getattr(radio, "board", None)
            tail = f" board={board}" if board else ""
            say(f"attached (link held since {since['t']}){tail}")
        try:
            while True:
                try:
                    data = await reader.read(4096)
                except OSError:
                    # The command hung up and the board spoke before we
                    # read the EOF: the write to it failed, and asyncio
                    # hands that failure to whoever is reading. Same thing
                    # as a clean close, so treat it as one.
                    break
                if not data:
                    break
                try:
                    await radio.write(data)
                except Exception as e:      # whatever bleak raises: the link is gone
                    say(f"the link dropped: {e}")
                    end("dropped")
                    break
        finally:
            if client["w"] is writer:
                client["w"] = None
            writer.close()

    # A file left by a bridge that was killed rather than ended would refuse
    # the bind; nobody serves it, or the client would not have launched us.
    try:
        os.unlink(sock_path)
    except FileNotFoundError:
        pass
    server = await asyncio.start_unix_server(serve, path=sock_path)
    ino = os.stat(sock_path).st_ino

    try:
        found = await radio.open(say, timeout)
    except Exception as e:                  # a connect that failed: say why, and log it
        traceback.print_exc()
        say(f"failed to attach: {e}")
        found = False
        end("failed")
    if found:
        since["t"] = time.strftime("%H:%M:%S")
    else:
        end("no board")

    why = await done
    server.close()
    if client["w"] is not None:
        client["w"].close()
    await server.wait_closed()
    try:
        await radio.close()
    except Exception:                       # already gone; nothing to undo
        pass
    # Only our own socket: a newer bridge may have replaced it by now.
    try:
        if os.stat(sock_path).st_ino == ino:
            os.unlink(sock_path)
    except OSError:
        pass
    return why


# ---- This half: the protocol, and the terminal ---------------------------

class Link:
    """The board, reached through the bridge. Lines in, lines out."""

    def __init__(self, timeout, sock_path=SOCK, launch=None, serial=None):
        self.timeout = timeout
        self.sock_path = sock_path
        self.launch = launch or (lambda: launch_bridge(timeout, serial))
        self.sock = None
        self.buf = ""
        self.lines = []
        self.lock = threading.Lock()
        self.alive = True

    def _dial(self, verb):
        """One hello. Returns (socket, 'ok') attached, else (None, answer);
        the answer is None when nothing was listening at all."""
        s = socket.socket(socket.AF_UNIX)
        s.settimeout(2.0)
        try:
            s.connect(self.sock_path)
            s.sendall(f"kble {HELLO_VERSION} {verb}\n".encode())
            line = b""
            while not line.endswith(b"\n"):
                c = s.recv(1)
                if not c:
                    break
                line += c
        except OSError:
            s.close()
            return None, None
        parts = line.decode("utf-8", "replace").split()
        if parts == ["kble", str(HELLO_VERSION), "ok"]:
            s.settimeout(None)
            return s, "ok"
        s.close()
        return None, (parts[2] if len(parts) == 3 else "stale")

    def _dial_attach(self):
        """`_dial("attach")`, patient with a `busy` that is only the previous
        command's socket waiting to be reaped - see BUSY_GRACE_S."""
        end = time.time() + BUSY_GRACE_S
        while True:
            sock, answer = self._dial("attach")
            if (answer != "busy") or (time.time() >= end):
                return sock, answer
            time.sleep(0.05)

    def start(self):
        busy = ("the bridge is busy: another kble is attached - one command "
                "at a time (or `kble.py detach`)")
        self.sock, answer = self._dial_attach()
        if answer == "busy":
            die(busy)
        if answer != "ok":
            # Nothing there, or a bridge from another version of this file
            # that is now stepping aside. Either way: launch, then keep
            # dialling until a current bridge says ok.
            self.launch()
            end = time.time() + self.timeout + 15.0
            while time.time() < end:
                self.sock, answer = self._dial_attach()
                if answer == "ok":
                    break
                if answer == "busy":
                    die(busy)
                time.sleep(0.1)
            else:
                hint = ("\n  a bridge may be stuck: `pkill -f 'kble.*--hold'` "
                        "and try again")
                if os.path.exists(LOG):
                    with open(LOG) as f:
                        tail = f.read().strip().splitlines()[-6:]
                    hint = "\n  " + "\n  ".join(tail) + hint
                die("the bridge never came up" + hint)
        threading.Thread(target=self._reader, daemon=True).start()

    def detach(self):
        """Tell a holding bridge to let go. False when none was there."""
        s, answer = self._dial("detach")
        if s is not None:
            s.close()
        return answer is not None

    def _reader(self):
        while self.alive:
            try:
                data = self.sock.recv(4096)
            except OSError:
                break
            if not data:
                break
            with self.lock:
                self.buf += data.decode("utf-8", "replace")
                while "\n" in self.buf:
                    line, self.buf = self.buf.split("\n", 1)
                    self.lines.append(line.rstrip("\r"))
        self.alive = False

    def take(self):
        with self.lock:
            out, self.lines = self.lines, []
        return out

    def send(self, text):
        self.sock.sendall((text + "\n").encode())

    def send_raw(self, data):
        self.sock.sendall(data)

    def wait_for(self, pred, timeout):
        """Collect lines until pred(line) is true. Returns (lines, hit)."""
        got, end = [], time.time() + timeout
        while time.time() < end:
            for line in self.take():
                got.append(line)
                if pred(line):
                    return got, line
            if not self.alive:
                break
            time.sleep(0.02)
        return got, None

    def close(self):
        """Shut the socket DOWN before closing it, because _reader() is
        blocked in recv() on this same socket from another thread.

        close() frees the descriptor; it does not necessarily end the
        connection. On Linux the blocked recv() still holds the underlying
        file description open, so no FIN reaches the bridge and it goes on
        believing this client is attached - the next command is then told the
        bridge is busy, forever, and pointed at a stuck bridge that does not
        exist. macOS tears the socket down at close() and hides the whole
        thing, which is why this only ever showed up off the bench.

        shutdown() acts on the connection rather than on the descriptor: the
        FIN goes out now and the blocked recv() returns empty.
        """
        self.alive = False
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except (OSError, AttributeError):
            pass                        # already gone, or never dialled
        try:
            self.sock.close()
        except (OSError, AttributeError):
            pass


def show(line):
    """Print one line the way a person wants to read it."""
    if line.startswith("\x00kble: "):
        print("\033[2m" + line[1:] + "\033[0m")
    else:
        print(line)


def is_terminator(line):
    return line == ">ok" or line.startswith(">err")


# ---- Subcommands ---------------------------------------------------------

def do_term(link, args):
    print("kble: type a command, or Ctrl-D to leave")
    stop = threading.Event()

    def pump():
        while not stop.is_set() and link.alive:
            for line in link.take():
                show(line)
            time.sleep(0.05)

    t = threading.Thread(target=pump, daemon=True)
    t.start()
    try:
        for typed in sys.stdin:
            typed = typed.rstrip("\n")
            if typed:
                link.send(typed)
    except KeyboardInterrupt:
        pass
    finally:
        # Answers lag the keystroke that asked for them, so leaving the
        # moment stdin closes would swallow the last one - which is exactly
        # what happens when this is driven from a pipe rather than typed.
        time.sleep(2.0)
        for line in link.take():
            show(line)
        stop.set()
        t.join(timeout=1.0)
    return 0


def do_cmd(link, args):
    line = " ".join(args.line)
    link.send(line)
    got, term = link.wait_for(is_terminator, args.timeout)
    for l in got:
        show(l)
    if term is None:
        die(f"no terminator for '{line}' in {args.timeout}s")
    return 0 if term == ">ok" else 1


def stream(link, cmd, data, what):
    """Send one payload verb's conversation: `cmd`, then `data` chunk by chunk.

    Identical conversation to the cable's, because it is the same console:
    `>|ready chunk=N`, then the data one chunk per `>|ack`. The stop-and-wait
    IS the flow control - the board bounds its memory to one chunk and its
    idle watchdog tells a slow host from a dead one. Returns the terminator.
    """
    link.send(cmd)

    got, ready = link.wait_for(
        lambda l: l.startswith(">|ready") or is_terminator(l), 15.0)
    for l in got:
        show(l)
    if ready is None:
        die(f"{what}: the board never answered the request")
    if is_terminator(ready):
        if "serial_only" in ready:
            die(f"this firmware refuses {what} over BLE "
                "(>err code=5 msg=serial_only) - it needs the raw-mode slice")
        die(f"{what}: the board refused: {ready}")

    chunk = CHUNK
    for tok in ready.split():
        if tok.startswith("chunk="):
            chunk = int(tok.split("=", 1)[1])

    sent = 0
    while sent < len(data):
        piece = data[sent:sent + chunk]
        link.send_raw(piece)
        sent += len(piece)
        pct = (100 * sent) // len(data)
        print(f"\rkble: {sent}/{len(data)} bytes ({pct}%)", end="", flush=True)
        if sent >= len(data):
            break                      # the last chunk earns no ack
        got, ack = link.wait_for(
            lambda l: l.startswith(">|ack") or is_terminator(l), 30.0)
        for l in got:
            if not l.startswith(">|ack"):
                show(l)
        if ack is None:
            print()
            die(f"{what}: no ack after {sent} bytes")
        if is_terminator(ack):
            print()
            die(f"{what}: the board gave up: {ack}")
    print()

    got, term = link.wait_for(is_terminator, 60.0)
    for l in got:
        show(l)
    if term is None:
        die(f"{what}: the transfer finished but the board never closed the command")
    return term


def do_install(link, args):
    """An install over the air: `install src="terminal"`, stream()'s
    conversation. An app boots unless --no-run; there is no `--dst c5`,
    because the C5 is the BLE radio and its update would end the link."""
    if args.no_run and args.dst != "app":
        die("install: --no-run is for --dst app; only an app has a run")
    with open(args.binfile, "rb") as f:
        image = f.read()
    crc = zlib.crc32(image) & 0xFFFFFFFF
    cmd = (f'install src="terminal" dst={args.dst} size={len(image)} '
           f'crc32={crc}')
    if args.dst == "app":
        cmd += " run=" + ("off" if args.no_run else "on")
        kp = _payload()
        name = kp.project_name(image)
        if name:
            cmd += f" name={kp.quote(name)}"
    term = stream(link, cmd, image, "install")
    return 0 if term == ">ok" else 1


def _assets_function(link, line, timeout):
    """Run one `assets_*` function: its `>|assets` records, then `>ok` or
    `>err` once the job has ended. Returns (records, terminator).

    The functions answer in records, so there is no log to read: whatever the
    job said is here, whatever the log level.
    """
    link.send(line)
    got, term = link.wait_for(is_terminator, timeout)
    records = [l[2:] for l in got if l.startswith(">|")]
    for l in got:
        show(l)
    return records, term


def do_assets_install(link, args):
    """Send Kody's pack in DIR over BLE: begin, one file_put per file, commit."""
    kp = _payload()
    plan = kp.assets_plan(args.dir)
    total = sum(len(data) for _, data in plan)
    for attempt in range(10):
        records, term = _assets_function(
            link, f'function name="assets_begin" arg={total}', 15.0)
        if term is not None and "msg=name" in term and attempt < 9:
            # kgui registers its functions once the panel is up.
            time.sleep(1.0)
            continue
        if term == ">ok":
            break
        if kp.assets_refusal(records) != "busy" or attempt == 9:
            die(f"assets_begin: {term}")
        time.sleep(1.0)
    for name, data in plan:
        crc = zlib.crc32(data) & 0xFFFFFFFF
        term = stream(link, f"file_put path={kp.quote('/storage/kode.new/' + name)} "
                            f"size={len(data)} crc32={crc}", data, "file_put")
        if term != ">ok":
            die(f"file_put {name}: {term}")
    _, term = _assets_function(link, 'function name="assets_commit"', 130.0)
    if term != ">ok":
        die(f"assets_commit: {term}")
    print(f"pack installed: {len(plan) - 1} files, {total} bytes")
    return 0


# ---- The other direction -------------------------------------------------
#
# `screenshot` and `file_get` send base64 inside ordinary `>|d=` records, and
# base64 is exactly why they work here at all: the BLE console splits what it
# sends on newlines, so a raw stream would have been cable-only. Same records,
# same decoding as kcli.py - see kpayload.py.

def _payload():
    """kpayload, imported where it is used and not at the top of the file.

    This script runs as two halves and only one of them decodes anything. The
    half inside the .app bundle is a dumb byte pipe, and it runs from a COPY of
    this file in the cache directory - where tools/ is not on the path and an
    import at module scope dies before the bridge ever starts. Asking for it
    here means the half that needs it gets it and the half that does not never
    looks.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import kpayload
    return kpayload


def collect_payload(link, idle):
    """Records until the terminator. Returns (header fields, bytes).

    An IDLE deadline, not a total one, because that is how the board measures
    it: its watchdog is re-armed on every slice, so a link that is merely slow
    is allowed to be. It matters more here than on the cable - a frame is
    about half a minute at 17 KB/s - and a total budget set for that would
    take half a minute to notice a board that had gone away.
    """
    kp = _payload()
    head, parts, end = None, [], time.time() + idle
    while time.time() < end:
        lines = link.take()
        if not lines:
            if not link.alive:
                die("the link dropped before the terminator")
            time.sleep(0.02)
            continue
        end = time.time() + idle             # progress, not total time
        for line in lines:
            if line.startswith(">|d="):
                parts.append(line[4:])
            elif line.startswith(">|"):
                head = line[2:]
                show(line)
            elif line == ">ok":
                f = kp.head_fields(head)
                want = f.get("len") or f.get("size")
                try:
                    return f, kp.decode_records(parts,
                                                int(want) if want else None)
                except kp.PayloadError as e:
                    die(str(e))
            elif line.startswith(">err"):
                die(line)
            else:
                show(line)
    die("the board went quiet before the terminator")


def do_screenshot(link, args):
    kp = _payload()
    if args.dst == "file":
        # The board writes it; nothing crosses the air but the result.
        link.send(f"screenshot dst=file name={kp.quote(args.name)}")
        got, term = link.wait_for(is_terminator, args.idle)
        for l in got:
            show(l)
        if term is None:
            die("the board never closed screenshot")
        if term != ">ok":
            return 1
        print(f"kble: on the board - `file-get {args.name}` to fetch it")
        return 0

    link.send("screenshot")
    _, data = collect_payload(link, args.idle)
    try:
        _, note = kp.write_capture(args.out, data)
    except kp.PayloadError as e:
        die(f"screenshot: {e}")
    print(f"kble: {note}")
    return 0


def do_file_get(link, args):
    link.send(f"file_get name={_payload().quote(args.name)}")
    f, data = collect_payload(link, args.idle)
    out = args.out or os.path.basename(args.name)
    with open(out, "wb") as fh:
        fh.write(data)
    print(f"kble: {out}: {len(data)} bytes from {f.get('medium', '?')}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--hold", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--serial", metavar="KODE...",
                    help=f"attach only to this board (default: ${SERIAL_ENV}, "
                         "else the first Kode scanned)")
    ap.add_argument("--timeout", type=float, default=25.0,
                    metavar="S", help="how long to look for the board "
                                      "(the first command only)")
    sub = ap.add_subparsers(dest="what")

    p = sub.add_parser("term", help="an interactive terminal")
    p.set_defaults(fn=do_term)

    p = sub.add_parser("cmd", help="one command, then its terminator")
    p.add_argument("line", nargs="+")
    p.add_argument("--cmd-timeout", dest="timeout", type=float, default=15.0)
    p.set_defaults(fn=do_cmd)

    p = sub.add_parser("install", help="stream a .bin and install it")
    p.add_argument("binfile")
    p.add_argument("--dst", choices=("app", "os", "pending"), default="app",
                   help="where it goes (default: app)")
    p.add_argument("--no-run", action="store_true",
                   help="with --dst app: install without launching it")
    p.set_defaults(fn=do_install)

    p = sub.add_parser("assets-install",
                       help="send Kody's pack in DIR and make it the Dot's")
    p.add_argument("dir", metavar="DIR")
    p.set_defaults(fn=do_assets_install)

    p = sub.add_parser("screenshot", help="what is on the panel, as a picture")
    p.add_argument("-o", "--out", default="shot.png", metavar="FILE",
                   help="where to write it. A .png name is converted (no "
                        "dependencies); anything else is saved as the BMP "
                        "the board sent. Default: shot.png")
    p.add_argument("--dst", choices=("console", "file"), default="console",
                   help="console: over the air to -o, about half a minute. "
                        "file: the board saves it to --name instead and "
                        "nothing is transferred")
    p.add_argument("--name", default="shot.bmp", metavar="FILE",
                   help="with --dst file, the name on the board")
    p.add_argument("--idle", type=float, default=30.0, metavar="S",
                   help="give up after S seconds of silence. Not a total "
                        "budget: a slow link is fine, a stopped one is not")
    p.set_defaults(fn=do_screenshot)

    p = sub.add_parser("file-get", help="fetch any file off the SD or the FAT")
    p.add_argument("name")
    p.add_argument("-o", "--out", metavar="FILE",
                   help="where to write it (default: the same basename)")
    p.add_argument("--idle", type=float, default=30.0, metavar="S",
                   help="give up after S seconds of silence")
    p.set_defaults(fn=do_file_get)

    sub.add_parser("detach", help="let go of the board; it goes back on "
                                  "the air by itself")

    args = ap.parse_args()

    if args.hold:                        # we are the half inside the bundle
        why = asyncio.run(hold(SOCK, BleakRadio(args.serial), args.timeout))
        print(f"bridge: {why}")           # into bridge.log
        return 0

    if args.what == "detach":
        if Link(args.timeout).detach():
            print("kble: let go - the board is back on the air by itself")
        else:
            print("kble: nothing was holding a link")
        return 0

    if not getattr(args, "fn", None):
        ap.print_help()
        return 2

    serial = norm_serial(args.serial or os.environ.get(SERIAL_ENV))
    link = Link(args.timeout, serial=serial)
    link.start()
    # Let the bridge say whether it has the board before a command goes out.
    got, _ = link.wait_for(
        lambda l: "attached" in l or "no board" in l or "failed" in l,
        args.timeout + 10.0)
    for l in got:
        show(l)
    why = None
    if any(l.lstrip("\x00").startswith("kble: attached") for l in got):
        why = wrong_board(got, serial)
    if why is not None:
        link.close()
        die(why)
    if not link.alive:
        die("the bridge closed - is the board advertising?")
    try:
        return args.fn(link, args)
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main())
