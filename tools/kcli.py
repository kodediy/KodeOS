#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kcli - the host side of the kodeOS console protocol.

The wire contract lives in https://docs.kode.diy/internals/console-protocol: one line is one
command, every response line starts with `>` (records `>|`, terminators
`>ok` / `>err`), everything else (`!`) is a log the host did not ask for.
This tool speaks that contract so bench scripts and editor tasks do not
have to re-implement line framing, echo discarding and ANSI stripping
every time.

    kcli.py -p PORT ping
    kcli.py -p PORT cmd "app_list"
    kcli.py -p PORT install path/to/app.bin
    kcli.py -p PORT install build/kodeos.bin --dst os
    kcli.py -p PORT install build_c5/kode_c5.bin --dst c5
    kcli.py -p PORT file-put notes.txt /storage/notes.txt
    kcli.py -p PORT screenshot -o shot.png
    kcli.py -p PORT file-get notes.txt -o notes.txt
    kcli.py -p PORT config
    kcli.py -p PORT config-export -o settings.json
    kcli.py -p PORT config-import settings.json

`install` and `file-put` are the two commands with a payload, and they
share exactly one conversation: after the board answers `>|ready chunk=<n>`
the raw bytes follow on the same wire, one chunk per `>|ack`, and the
closing terminator only arrives once the transfer checksummed and the write
- an install or a plain file - verified. The stop-and-wait is the flow
control - the wire itself has none (the device ACKs USB at line rate and
silently drops what its RX ring cannot hold), so never send ahead of the
ack.

Needs pyserial (any ESP-IDF environment has it).
"""

import argparse
import base64
import json
import os
import re
import signal
import subprocess
import sys
import time
import zlib

from kpayload import (PayloadError, assets_plan, assets_refusal, decode_records,
                      fields, head_fields, project_name, quote, write_capture)

try:
    import serial
except ImportError:
    sys.exit("kcli: pyserial is not installed - run inside an IDF environment")

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
# The sequence AO_Log emits to wipe a half-drawn prompt before a record:
# \r ESC [K. Semantically it DISCARDS everything drawn before it on the
# line ("kode> ", usually), so the parser must keep only what follows -
# merely deleting the control bytes would leave the prompt text glued to
# the front of the record, and ">|ready" hiding behind "kode> " was a real
# bug that cost an afternoon.
ERASE = "\r\x1b[K"

READY_TIMEOUT = 12.0   # covers the region work before >|ready; one retry after
CLOSE_TIMEOUT = 30.0   # CRC + image verify + directory commit after the bytes
C5_CLOSE_IDLE = 45.0   # install --dst c5: silence this long after the bytes means the
                       # update died; the link's bring-up alone is ~10 s quiet


def port_holders(port):
    """PIDs holding `port` open, per lsof. Empty when it is free (or on any
    lsof hiccup - the caller is about to find out the honest way anyway).

    On macOS every serial device has two nodes, tty.* and cu.*, and holding
    either one makes opening the other fail with EBUSY - so both spellings
    are checked, whichever one the caller was given.
    """
    names = {port}
    base = os.path.basename(port)
    if base.startswith("tty."):
        names.add(os.path.join(os.path.dirname(port), "cu." + base[4:]))
    elif base.startswith("cu."):
        names.add(os.path.join(os.path.dirname(port), "tty." + base[3:]))
    try:
        out = subprocess.run(["lsof", "-t", *sorted(names)],
                             capture_output=True, text=True, timeout=5).stdout
        return sorted({int(pid) for pid in out.split()})
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return []


class Board:
    """One serial connection speaking the kcli framing."""

    def __init__(self, port, verbose=False, steal=False):
        # Deassert DTR/RTS before opening: some hosts otherwise pulse them
        # and reset the board just by attaching.
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = 115200
        self.ser.timeout = 0.2
        self.ser.dtr = False
        self.ser.rts = False
        try:
            self.ser.open()
        except serial.SerialException as e:
            if "usy" not in str(e):     # Resource busy / Device busy
                raise
            holders = port_holders(port)
            if not steal:
                who = ", ".join(str(p) for p in holders) or "unknown pid"
                die(f"{port} is held by another process ({who}) - close the "
                    f"monitor, or run with --steal to close it for you")
            # A stray monitor owns the port (the usual bench culprit). Ask it
            # to leave, give the device a moment to come back, retry once.
            for pid in holders:
                print(f"kcli: {port} held by pid {pid}; terminating it")
                try:
                    os.kill(pid, signal.SIGTERM)
                except OSError:
                    pass
            time.sleep(1.0)
            self.ser.open()
        self.verbose = verbose
        self.buf = b""
        self.pos = 0

    def close(self):
        self.ser.close()

    def reopen(self):
        """Close/open cycle. On this board attaching resets it, which is
        sometimes the point (see the pre-flight retry in install)."""
        if self.ser.is_open:
            self.ser.close()
        self.buf = b""
        self.pos = 0
        self.ser.open()

    def drain(self, seconds=0.3):
        """Discard whatever the board is saying (boot logs, old echo)."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if not self.ser.read(4096):
                break
        self.buf = b""
        self.pos = 0

    def send_line(self, line):
        if self.verbose:
            print(f"<- {line}")
        self.ser.write(line.encode() + b"\r\n")
        self.ser.flush()

    def read_line(self, deadline):
        """One wire line, stripped of ANSI/erase noise. None on timeout.

        The read cursor is an index and not a slice, and that is not
        micro-optimisation. Re-slicing the buffer per line is O(n) in the
        bytes still held, so a `screenshot` - half a megabyte in 2,542 lines -
        spends its time copying instead of reading, the OS serial buffer
        overflows, and the host loses bytes the board never dropped. It shows
        up as base64 that is one character past a multiple of four.
        """
        while time.monotonic() < deadline:
            nl = self.buf.find(b"\n", self.pos)
            if nl >= 0:
                raw = self.buf[self.pos:nl]
                self.pos = nl + 1
                if self.pos > (1 << 16):     # compact rarely, not per line
                    self.buf = self.buf[self.pos:]
                    self.pos = 0
                text = raw.decode(errors="replace")
                if ERASE in text:
                    text = text.split(ERASE)[-1]   # keep what the erase reveals
                text = ANSI.sub("", text).strip("\r")
                if text:
                    return text
                continue
            # read(1) returns on the first byte (or timeout); in_waiting
            # then drains the rest of the burst without ever sitting out the
            # full timeout - the difference between ~5 ms and ~200 ms per
            # ack round trip.
            chunk = self.ser.read(max(1, self.ser.in_waiting))
            if chunk:
                self.buf += chunk
        return None

    def collect(self, timeout, stop=None, idle=False):
        """Response lines until the terminator (or `stop` matches).

        Echo and `!` log lines pass through to the terminal in verbose mode
        and are never part of the result. Returns (records, terminator);
        terminator is None if the deadline passed first.

        `idle=True` makes `timeout` a deadline on silence rather than on the
        whole wait: every line the board says, `!` progress included, pushes
        it out again. That is what a verb that works for a minute or two and
        reports as it goes needs - `install dst=c5` pushes 32 KB at a time and
        then rebuilds a link - where a fixed total would either give up on a
        slow update or sit on a dead one for as long as the slowest good one.
        """
        deadline = time.monotonic() + timeout
        records = []
        while True:
            line = self.read_line(deadline)
            if line is None:
                return records, None
            if idle:
                deadline = time.monotonic() + timeout
            if line.startswith(">|"):
                records.append(line[2:])
                if stop and stop(line[2:]):
                    return records, line
            elif line.startswith(">ok") or line.startswith(">err"):
                return records, line
            elif self.verbose:
                print(f"   {line}")

    def command(self, line, timeout=5.0):
        """Send one ordinary command, return (records, terminator)."""
        self.send_line(line)
        return self.collect(timeout)

    def follow(self, seconds):
        """Print whatever the board says for a while, and keep it.

        Not drain(): a verb that answers `>ok` on accept reports what it
        actually did afterwards, as `!` log lines. Throwing those away would
        turn a firmware write into a command that says nothing and takes no
        time.
        """
        deadline = time.monotonic() + seconds
        while True:
            line = self.read_line(deadline)
            if line is None:
                return
            print(f"   {line}")


# ---- Payload verbs (screenshot, file-get) ---------------------------------
#
# The board sends bulk data as base64 inside ordinary `>|d=` records rather
# than as a raw stream, so it survives `!` log lines landing in the middle of
# it and works the same over BLE. A frame is ~2,500 records. The decoding and
# the PNG live in kpayload.py, shared with kble.py: only the pipe differs.

def collect_payload(board, idle):
    """Records until the terminator, decoded. Returns (header, bytes).

    The deadline is per line, not for the whole transfer, because that is how
    the board measures it too: its watchdog is re-armed on every slice, so a
    transfer that is merely slow (about three seconds on the cable, around
    half a minute over BLE) is fine and one that has stopped is not. A single
    total budget would have to be set for the worst link and would then fail
    to notice a dead one.
    """
    head = None
    parts = []
    while True:
        line = board.read_line(time.monotonic() + idle)
        if line is None:
            die("payload: the board went quiet before the terminator")
        if line.startswith(">|d="):
            parts.append(line[4:])
        elif line.startswith(">|"):
            head = line[2:]
        elif line.startswith(">ok"):
            f = head_fields(head)
            want = f.get("len") or f.get("size")
            try:
                return f, decode_records(parts,
                                         int(want) if want else None)
            except PayloadError as e:
                die(str(e))
        elif line.startswith(">err"):
            die(line)
        elif board.verbose:
            print(f"   {line}")


def do_screenshot(board, args):
    if args.dst == "file":
        # The board writes it; nothing comes down the wire but the result.
        records, term = board.command(f"screenshot dst=file name={quote(args.name)}",
                                      timeout=args.idle)
        check_ok(term, "screenshot")
        for r in records:
            print(r)
        print(f"ok (on the board - `file-get {args.name}` to fetch it)")
        return

    board.send_line("screenshot")
    _, data = collect_payload(board, args.idle)
    try:
        _, note = write_capture(args.out, data)
    except PayloadError as e:
        die(f"screenshot: {e}")
    print(note)


def do_file_get(board, args):
    board.send_line(f"file_get name={quote(args.name)}")
    f, data = collect_payload(board, args.idle)
    out = args.out or os.path.basename(args.name)
    with open(out, "wb") as fh:
        fh.write(data)
    print(f"{out}: {len(data)} bytes from {f.get('medium', '?')}")


def die(msg):
    sys.exit(f"kcli: {msg}")


def check_ok(term, what):
    if term is None:
        die(f"{what}: no terminator (timeout)")
    if not term.startswith(">ok"):
        die(f"{what}: {term}")


def do_ping(board, args):
    records, term = board.command("ping")
    check_ok(term, "ping")
    print(records[0] if records else "pong")


def do_cmd(board, args):
    """Send one or more command lines down a single open session.

    One session and not one per line, because opening this port resets the
    board: `radio_up` followed by a second invocation would find a freshly
    rebooted board with no link and no address. Anything that builds state
    has to be said in one breath.

    --gap is what turns that into something an editor task can use. A verb
    like `install` off a medium or `radio_up` answers `>ok` the moment it is
    accepted and then works for the next half minute, reporting as `!` log
    lines; the gap is how long to keep listening before moving on.
    """
    gaps = args.gap or [0.0]
    for i, line in enumerate(args.line):
        records, term = board.command(line, timeout=args.timeout)
        for r in records:
            print(r)
        check_ok(term, line.split()[0] if line.split() else "cmd")
        gap = gaps[i] if i < len(gaps) else gaps[-1]
        if gap > 0.0:
            board.follow(gap)
        if i + 1 < len(args.line):
            print("--")
    print("ok")


def stream_payload(board, data, cmd, what, verbose=False,
                   close_timeout=None, close_idle=False):
    """Speak the payload verbs' wire conversation and send `data`.

    `install src="terminal"` and `file_put` are one conversation, only the
    opening line differs: `cmd` gets the board to a
    `>|ready chunk=<n>`, then `data` goes across in windows of that size,
    each one closed out by `>|ack`, and the whole thing finishes on the
    terminator that follows the last chunk. `what` is only for the error
    messages, so a failure names which verb it was talking. The caller
    checks the local file and prints its own final line - this only owns
    the wire.

    `close_timeout` is how long to wait for that terminator after the last
    byte (CLOSE_TIMEOUT unless said otherwise), and with `close_idle` it is
    a deadline on silence instead - see Board.collect().
    """
    size = len(data)

    # Make sure the console is alive and idle before committing to a mode
    # where a garbled command would leave us streaming at a line editor.
    # A dead ping usually means the board is sitting in a user app with no
    # console. Attaching to the port resets this board, and any reset comes
    # home to kodeOS - so one reopen is the retry.
    _, term = board.command("ping")
    if term is None or not term.startswith(">ok"):
        print("kcli: no console (a user app?); reopening the port to reset")
        board.close()
        time.sleep(1.0)
        board.reopen()
        end = time.monotonic() + 5.0
        while time.monotonic() < end:      # let kodeOS boot; show its log
            line = board.read_line(end)
            if line is not None and verbose:
                print(f"   {line}")
        board.drain()
        _, term = board.command("ping")
    check_ok(term, "ping (pre-flight)")

    line = None
    for attempt in (1, 2):
        board.send_line(cmd)
        records, line = board.collect(READY_TIMEOUT,
                                      stop=lambda r: r.startswith("ready"))
        for rec in records:
            if rec.startswith("evicted"):
                print(f"   {rec}")
        if line is not None and line.startswith(">err"):
            die(f"{what}: {line}")
        if line is not None:
            break
        if attempt == 1:
            # The ready never made it out (a rare loss on the board's TX
            # path). Reopening resets the board, which clears any armed
            # transfer - so a clean retry is always safe.
            print("kcli: no ready; resetting the board and retrying once")
            board.reopen()
            end = time.monotonic() + 5.0
            while time.monotonic() < end:
                l = board.read_line(end)
                if l is not None and verbose:
                    print(f"   {l}")
            board.drain()
            _, term = board.command("ping")
            check_ok(term, "ping (retry)")
    if line is None:
        die(f"{what}: board never said ready (after a retry)")

    # The window the board announced: send that many bytes, wait for its
    # `>|ack`, repeat. The wire itself has no flow control (the device ACKs
    # USB at line rate and drops what its 512-byte ring cannot hold), so the
    # ack - sent only once a chunk is safely in flash - is the entire reason
    # the transfer arrives whole.
    m = re.search(r"\bchunk=(\d+)", line)
    step = int(m.group(1)) if m else 4096

    t0 = time.monotonic()
    sent = 0
    while sent < size:
        n = len(data[sent:sent + step])
        board.ser.write(data[sent:sent + n])
        board.ser.flush()
        sent += n
        if n == step:
            _, term = board.collect(10.0, stop=lambda r: r == "ack")
            if term is None:
                die(f"{what}: no ack at {sent}/{size} bytes")
            if term.startswith(">err"):
                die(f"{what}: {term}")
        pct = 100 * sent // size
        print(f"\r-> {sent}/{size} bytes ({pct}%)", end="", flush=True)
    dt = time.monotonic() - t0
    rate = size / dt / 1024 if dt > 0 else 0
    print(f"\r-> {size} bytes in {dt:.1f}s ({rate:.0f} KB/s)")

    if close_timeout is None:
        close_timeout = CLOSE_TIMEOUT
    _, term = board.collect(close_timeout, idle=close_idle)
    check_ok(term, what)


def do_install(board, args):
    """Stream an image over this cable and install it where `--dst` says.

    `install src="terminal"`: the `>ok` is a result - written and verified -
    and what follows it is the destination's own ending.

    `app` installs into the apps region and boots the app, unless
    `--no-run` (the board's own default is run=off; this tool asks for the
    boot, which is what installing from a computer is nearly always for).

    `os` writes the inactive kodeOS slot and the board resets into it right
    behind the `>ok`, so nothing after this line finds a console for a couple
    of seconds. The new image confirms itself at its first frame, or 15 s
    after boot on a board with no glass; any reset before that - opening this
    port is one - rolls it back to the slot before, and `boot_info`
    afterwards says which slot is running.

    `c5` lands the bytes in the P4's PSRAM first and verifies them there,
    which is why the acks come fast; the update itself starts after the last
    one - the push over the SDIO link and the C5 activating the image - and
    that is what the `>ok` waits for, a few seconds with the link already up
    and ten or so more when it has to be brought up first, which is why the
    close is a deadline on silence rather than on the whole wait. The `>ok`
    cannot say the C5 is RUNNING the image: esp-hosted builds its link once
    per boot, so the update ends with it down. Reset the board, then `c5`
    names what the C5 runs. Cable only: the C5 is the BLE radio.

    `pending` parks the bytes undecoded in the mailbox; `install
    src="pending" dst=...` installs them later.

    For an app the image's project name goes too, so a full region gives up
    the app's own old copy first; every app the install evicts instead is
    printed as an `evicted` record before the bytes go.
    """
    if args.no_run and args.dst != "app":
        die("install: --no-run is for --dst app; only an app has a run")
    data = open(args.binfile, "rb").read()
    size = len(data)
    crc = zlib.crc32(data)
    cmd = f'install src="terminal" dst={args.dst} size={size} crc32={crc}'
    if args.dst == "app":
        cmd += " run=" + ("off" if args.no_run else "on")
        name = project_name(data)
        if name:
            cmd += f" name={quote(name)}"
    if args.dst == "c5":
        stream_payload(board, data, cmd, "install", verbose=args.verbose,
                       close_timeout=C5_CLOSE_IDLE, close_idle=True)
    else:
        stream_payload(board, data, cmd, "install", verbose=args.verbose)
    if args.dst == "app":
        print("installed" + ("" if args.no_run else " and booting"))
    elif args.dst == "os":
        print("installed into the inactive slot; the board is rebooting "
              "into it")
    elif args.dst == "c5":
        print("installed and activated; the C5 is booting it. Reset the "
              "board to bring the link back, then `c5` names what it runs")
    else:
        print("parked in the mailbox")


def do_file_put(board, args):
    data = open(args.local, "rb").read()
    size = len(data)
    crc = zlib.crc32(data)
    cmd = f"file_put path={quote(args.remote)} size={size} crc32={crc}"
    stream_payload(board, data, cmd, "file_put", verbose=args.verbose)
    print(f"{args.remote}: {size} bytes")


def _assets_function(board, line, timeout):
    """Run one `assets_*` function: its `>|assets` records, then `>ok` or
    `>err` once the job has ended. Returns (records, terminator).

    The functions answer in records, so there is no log to read: whatever the
    job said is here, whatever the log level.
    """
    records, term = board.command(line, timeout)
    for rec in records:
        print(f"   {rec}")
    return records, term


def do_assets_install(board, args):
    """Send Kody's pack in DIR to /storage/kode.new/ and make it the FAT's."""
    plan = assets_plan(args.dir)
    total = sum(len(data) for _, data in plan)
    for attempt in range(10):
        records, term = _assets_function(
            board, f'function name="assets_begin" arg={total}', 15.0)
        if term is not None and "msg=name" in term and attempt < 9:
            # kgui registers its functions once the panel is up: a board
            # that has just booted does not know the name yet.
            time.sleep(1.0)
            continue
        if term is not None and term.startswith(">ok"):
            break
        if assets_refusal(records) != "busy" or attempt == 9:
            die(f"assets_begin: {term or 'no terminator (timeout)'}")
        time.sleep(1.0)
    for name, data in plan:
        stream_payload(board, data,
                       f"file_put path={quote('/storage/kode.new/' + name)} "
                       f"size={len(data)} crc32={zlib.crc32(data)}",
                       "file_put", verbose=args.verbose)
        print(f"   sent {name}: {len(data)} bytes")
    _, term = _assets_function(board, 'function name="assets_commit"', 130.0)
    check_ok(term, "assets_commit")
    print(f"pack installed: {len(plan) - 1} files, {total} bytes")


# ---- Settings (config) -----------------------------------------------------
#
# The board keeps every setting in NVS, each under the AO that owns it; `config`
# lists them and sets one at a time. A JSON file is only this tool's way of
# keeping a copy: the board never reads one.

CONFIG_TIMEOUT = 40.0   # the listing asks each owner in turn, 1 s each at most,
                        # and the registry holds 32 settings (KODE_CONFIG_MAX)


def config_read(board):
    """Every setting the board has, in its order: {key: (value, quoted)}.

    `quoted` says whether the value travels in quotes, which a JSON file
    cannot say for itself; config-import learns it here before it writes.
    A setting whose owner did not answer reads `-` and is left out.
    """
    records, term = board.command("config", timeout=CONFIG_TIMEOUT)
    check_ok(term, "config")
    out = {}
    for rec in records:
        if not rec.startswith("config "):
            continue
        f = fields(rec)
        if f.get("value", "-") == "-":
            print(f"kcli: config: {f.get('key')} did not answer")
            continue
        quoted = rec.split(" value=", 1)[1].startswith('"')
        out[f["key"]] = (f["value"], quoted)
    return out


def do_config(board, args):
    records, term = board.command("config", timeout=CONFIG_TIMEOUT)
    check_ok(term, "config")
    for line in records:
        print(line)


def do_config_export(board, args):
    data = {}
    for key, (value, quoted) in config_read(board).items():
        if not quoted and re.fullmatch(r"-?[0-9]+", value):
            data[key] = int(value)
        else:
            data[key] = value
    with open(args.output, "w") as fh:
        json.dump(data, fh, indent=2)
        fh.write("\n")
    print(f"kcli: {len(data)} settings -> {args.output}")


def do_config_import(board, args):
    with open(args.file) as fh:
        wanted = json.load(fh)
    if not isinstance(wanted, dict):
        die(f"config-import: {args.file} is not one JSON object")
    current = config_read(board)
    changed = 0
    for key, value in wanted.items():
        if key not in current:
            print(f"kcli: config-import: the board has no setting {key!r}; skipped")
            continue
        if isinstance(value, bool):
            word = "on" if value else "off"
        else:
            word = str(value)
        old, quoted = current[key]
        if word == old:
            continue
        if quoted:
            arg = quote(word)
        elif re.fullmatch(r"\S+", word):
            arg = word
        else:
            die(f"config-import: {key}: {word!r} is not one word")
        records, term = board.command(f"config key={quote(key)} value={arg}")
        check_ok(term, f"config {key}")
        print(f"{key}: {old} -> {word}")
        changed += 1
    print(f"kcli: {changed} settings changed")


def do_appdata_ls(board, args):
    records, term = board.command("appdata_ls")
    check_ok(term, "appdata_ls")
    for line in records:
        print(line)


def do_appdata_info(board, args):
    records, term = board.command(f"appdata_info name={quote(args.name)}")
    check_ok(term, "appdata_info")
    for line in records:
        print(line)


def do_appdata_rm(board, args):
    records, term = board.command(f"appdata_rm name={quote(args.name)}")
    check_ok(term, "appdata_rm")
    for line in records:
        print(line)


def _appdata_mkdir(board, path):
    """mkdir path="<path>", tolerating a directory that is already there.

    A taken name comes back `>err ... msg=exists` either way - mkdir()'s own
    EEXIST does not say whether what is there is a directory or a file
    (storage_ao.c's status_of_create_errno()) - so a stat settles which one
    it was before this is allowed to call it success.
    """
    records, term = board.command(f"mkdir path={quote(path)}")
    if term is not None and term.startswith(">ok"):
        return
    if term is not None and "msg=exists" in term:
        records, term = board.command(f"stat path={quote(path)}")
        if (term is not None and term.startswith(">ok")
                and any("type=dir" in r for r in records)):
            return
        die(f"appdata put: {path}: exists and is not a directory")
    die(f"appdata put: mkdir {path}: {term}")


def do_appdata_put(board, args):
    import kappdata
    obj = kappdata.validate(json.load(open(os.path.join(args.dir, "app.json"))))
    icon_path = kappdata.find_icon(args.dir, obj)
    icon = kappdata.to_k64(icon_path) if icon_path else None
    if icon is not None:
        obj["icon"] = "icon.k64"          # the board reads what we send, not what we found
    body = json.dumps(obj, separators=(", ", ": ")).encode()
    if len(body) > 1024:
        raise SystemExit("app.json: longer than 1 KB once written")
    base = kappdata.folder(args.name)
    for d in ("/storage/.kode", "/storage/.kode/appsdata", base):
        _appdata_mkdir(board, d)   # 'exists' is fine; mkdir is one level at a time
    stream_payload(board, body,
                  f"file_put path={quote(base + '/app.json')} size={len(body)} "
                  f"crc32={zlib.crc32(body)}",
                  "file_put", verbose=args.verbose)
    if icon is not None:
        stream_payload(board, icon,
                      f"file_put path={quote(base + '/icon.k64')} size={len(icon)} "
                      f"crc32={zlib.crc32(icon)}",
                      "file_put", verbose=args.verbose)
    records, term = board.command(f"appdata_info name={quote(args.name)}")
    check_ok(term, "appdata_info")
    for line in records:
        print(line)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-p", "--port", required=True, help="serial port")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="show logs and echo while waiting")
    ap.add_argument("--steal", action="store_true",
                    help="if the port is busy, terminate whoever holds it "
                         "(usually a stray monitor) and retry")
    ap.add_argument("--settle", type=float, default=0.0, metavar="S",
                    help="wait S seconds after opening the port before the "
                         "first command, so a board that is still booting "
                         "gets to finish (its boot log is shown, then "
                         "drained)")
    sub = ap.add_subparsers(dest="verb", required=True)

    sub.add_parser("ping", help="liveness check").set_defaults(fn=do_ping)

    p = sub.add_parser("cmd",
                       help="send one or more command lines in ONE session")
    p.add_argument("line", nargs="+")
    p.add_argument("--timeout", type=float, default=5.0)
    p.add_argument("--gap", type=float, action="append", metavar="SECONDS",
                   help="keep listening this long after a line - what a verb "
                        "that answers on accept and works afterwards (install "
                        "off a medium, radio_up) needs. Repeat it to give each "
                        "line its own wait; the last one given covers the "
                        "rest. Joining a network takes about 25s, writing "
                        "1.5MB about 60.")
    p.set_defaults(fn=do_cmd)

    p = sub.add_parser("install",
                       help="stream a .bin over the console and install it: "
                            "an app (and boot it), kodeOS into the inactive "
                            "slot, the ESP32-C5's firmware, or the mailbox")
    p.add_argument("binfile")
    p.add_argument("--dst", choices=("app", "os", "c5", "pending"),
                   default="app",
                   help="where it goes (default: app)")
    p.add_argument("--no-run", action="store_true",
                   help="with --dst app: install only; do not boot it")
    p.set_defaults(fn=do_install)

    p = sub.add_parser("assets-install",
                       help="send Kody's pack in DIR (kode.json and its files) "
                            "and make it the one on the Dot's FAT")
    p.add_argument("dir", metavar="DIR")
    p.set_defaults(fn=do_assets_install)

    p = sub.add_parser("file-put",
                       help="stream a local file onto the board (SD or FAT)")
    p.add_argument("local", metavar="LOCAL", help="local file to send")
    p.add_argument("remote", metavar="REMOTE",
                   help="path on the board, e.g. /storage/notes.txt or "
                        "/sdcard/notes.txt")
    p.set_defaults(fn=do_file_put)

    p = sub.add_parser("screenshot", help="what is on the panel, as a picture")
    p.add_argument("-o", "--out", default="shot.png", metavar="FILE",
                   help="where to write it. A .png name is converted (no "
                        "dependencies); anything else is saved as the BMP "
                        "the board sent. Default: shot.png")
    p.add_argument("--dst", choices=("console", "file"), default="console",
                   help="console: down the wire to -o. file: the board saves "
                        "it to --name instead and nothing is transferred - "
                        "which is what you want when you cannot be attached "
                        "at the moment worth capturing")
    p.add_argument("--name", default="shot.bmp", metavar="FILE",
                   help="with --dst file, the name on the board")
    p.add_argument("--idle", type=float, default=15.0, metavar="S",
                   help="give up after S seconds of silence. Not a total "
                        "budget: a slow link is fine, a stopped one is not")
    p.set_defaults(fn=do_screenshot)

    p = sub.add_parser("file-get", help="fetch any file off the SD or the FAT")
    p.add_argument("name")
    p.add_argument("-o", "--out", metavar="FILE",
                   help="where to write it (default: the same basename)")
    p.add_argument("--idle", type=float, default=15.0, metavar="S",
                   help="give up after S seconds of silence")
    p.set_defaults(fn=do_file_get)

    sub.add_parser("config", help="every setting the board keeps"
                   ).set_defaults(fn=do_config)
    p = sub.add_parser("config-export", help="the settings, into a JSON file")
    p.add_argument("-o", "--output", required=True)
    p.set_defaults(fn=do_config_export)
    p = sub.add_parser("config-import",
                       help="set the board to a JSON file's settings; only "
                            "the keys that differ are written")
    p.add_argument("file")
    p.set_defaults(fn=do_config_import)

    p = sub.add_parser("appdata",
                       help="the board's app data folders (icon, colour, name...)")
    ad = p.add_subparsers(dest="ad_verb", required=True)
    q = ad.add_parser("ls", help="every project with a binary or a folder")
    q.set_defaults(fn=do_appdata_ls)
    q = ad.add_parser("info", help="what the board resolved for one app")
    q.add_argument("name")
    q.set_defaults(fn=do_appdata_info)
    q = ad.add_parser("rm", help="remove the internal folder")
    q.add_argument("name")
    q.set_defaults(fn=do_appdata_rm)
    q = ad.add_parser("put", help="provision DIR (app.json and an image) for NAME")
    q.add_argument("name")
    q.add_argument("dir")
    q.set_defaults(fn=do_appdata_put)

    args = ap.parse_args()
    board = Board(args.port, verbose=args.verbose, steal=args.steal)
    try:
        if args.settle > 0:
            # Show what the board says while it settles (boot logs, usually),
            # then start from a clean buffer.
            end = time.monotonic() + args.settle
            while time.monotonic() < end:
                line = board.read_line(end)
                if line is not None:
                    print(f"   {line}")
        board.drain()
        args.fn(board, args)
    finally:
        board.close()


if __name__ == "__main__":
    main()
