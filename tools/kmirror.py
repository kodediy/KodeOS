#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kmirror - a browser viewer for the kodeOS screen mirror, with click-to-touch.

The board streams the panel as `mirror_start scale= q=`
(https://docs.kode.diy/internals/mirror); the wire contract is: console lines `>|m=<base64>`
carry a continuous stream - a 14-byte little-endian header (seq u16, x u16,
y u16, w u16, h u16, len u32) followed by that many JPEG bytes, then the
next header, with no terminator between frames). `>|mabort` means the board
gave up on the frame in progress and the next bytes start a fresh header;
`>|mpause` means it put the session to sleep and only `mirror_start` brings
it back. Both control records ride the console line on BOTH transports.
The host must send `mirror_ack seq=<n>` for every completed frame or the
credit window closes and the stream stops after four frames - a viewer that
does not ack is a screenshot, not a mirror.

Only the FIRST frame after `mirror_start` is forced full-panel (x=0, y=0,
covering the whole scaled canvas); the mirror is delta-based; so a frame
after that may cover only the rectangle that changed. That is why this
tool does not serve a plain MJPEG `<img src="/stream">` - swapping the
whole image to a small changed corner would make the picture flicker down
to that corner and back. Instead /stream relays kodeOS's own header+JPEG
framing verbatim (already reassembled, no base64 layer) and a small amount
of JavaScript composites each part onto a <canvas> at the offset its own
header carries, which is correct whether a given frame turns out to be the
whole panel or a sliver of it. The board's frames are already JPEG, so
nothing here decodes or re-encodes an image.

Two transports, one HTTP server:

    kmirror.py -p /dev/cu.usbmodem101                cable
    kmirror.py -p /dev/cu.usbmodem101 --scale 1 --q 85 --open
    kmirror.py --ble                                 over the air

`-p` needs pyserial, the same as kcli.py (imported from there rather than
reimplemented - one Board class, one place that knows the erase-sequence
trap and the O(1) read buffer, not a second copy of either).

`--ble` needs the firmware to see something subscribed to the mirror's own
GATT characteristic (`me->ble = mirror_ble_attached()` in display_ao.c
picks the transport on exactly that) - kble.py's own bridge only ever
subscribes to the console characteristic, so it does not make that true.
This file's BLE transport is its own small bridge, built the same way
kble.py's is (see kblebundle.py) but a separate one: a second signed .app
under the cache, which earns its own one-time macOS Bluetooth permission
prompt the first time it runs - expect it, it is normal. The BLE wire has
no base64 layer at all: a frame is the same 14-byte header immediately
followed by that many raw JPEG bytes, concatenated across notifications
exactly the way `>|m=` runs are concatenated on the cable. There is no
control marker inside that stream at all: `>|mabort` and `>|mpause` arrive
on the console characteristic, which the bridge subscribes to anyway, for
the reason BleFrameDecoder's docstring gives - a marker framed by length
cannot be told from the body it follows. Only one BLE central may be
attached to the board at a time - clear a stuck kble.py bridge first (`pkill -f 'kble.*--hold'`) if
this refuses to attach.
"""

import argparse
import asyncio
import binascii
import http.server
import os
import socket
import socketserver
import struct
import subprocess
import sys
import threading
import time
import urllib.parse

# kcli.py (pyserial) and kpayload.py are imported lazily, inside the
# functions that actually need them - see _board_module() and
# MirrorDecoder.feed(). This file also runs, unmodified, as the BLE
# bridge's own copy of itself inside a venv that has bleak and nothing
# else: an unconditional `from kcli import Board` at module scope would
# make --ble-bridge mode fail before it ever reached the code that does
# not need pyserial at all.

# The glass, in physical pixels - kbsp's KBSP_LCD_H_RES/V_RES. A tap must
# land in this box; the browser only ever knows the scaled-down frame.
PANEL_W = 410
PANEL_H = 502

# The six buttons, in kdrv_button_t's own order and by kdrv_button_name()'s
# own words - named rather than numbered, because that is the decision the
# firmware already made and a second spelling here would be a second truth.
BUTTON_IDS = ("up", "down", "left", "right", "top", "bot")

# How long a press may go unanswered before the viewer assumes the stream has
# stopped and restarts it. Under the board's own five-second credit stall, so
# the recovery beats the diagnosis; over the round trip a busy cable actually
# takes, so ordinary slowness is not mistaken for death.
REARM_AFTER = 2.0


def wait_ready(board, seconds, verbose=False):
    """Wait until the console answers, or give up.

    Polling `ping` rather than sleeping a fixed number of seconds: a warm
    board answers immediately and a cold one takes as long as it takes,
    and neither case wants the other's delay. The board's own boot log
    streams past in the meantime.

    What is still buffered when the console finally answers is exactly the
    tail of that boot log - the console is up long before the panel is, so
    the buffer holds AO_Display, AO_Touch and kgui coming up - and
    Board.drain() throws it away. A verbose run keeps it instead: it is the
    half of the boot a UI session most wants to read back, and it went
    missing from five of seven boots in one bench session (2026-09-15).
    """
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        _, term = board.command("ping", timeout=1.5)
        if term is not None and term.startswith(">ok"):
            if verbose:
                tail = time.monotonic() + 0.3
                while True:
                    line = board.read_line(tail)
                    if line is None:
                        break
                    print(f"   {line}")
            else:
                board.drain()
            return True
    return False


def find_port():
    """Guess the board's serial port, because it moves.

    This board re-enumerates on almost every reset - the same Dot has been
    /dev/cu.usbmodem101, 1101 and 3101 within an afternoon - so a port
    written down anywhere goes stale, including VS Code's own `idf.port`.
    Rather than fail with "no such file", look for what is actually there.

    Only usbmodem devices are considered: cu.Bluetooth-Incoming-Port and
    cu.debug-console are always present on a Mac and are never the board.
    A single candidate is returned; several means a real choice the caller
    has to make, so it says so rather than picking one and being wrong
    silently.
    """
    import glob
    found = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(found) == 1:
        return found[0]
    if not found:
        die("no /dev/cu.usbmodem* found - is the board plugged in?")
    die("several boards attached (%s) - name one with -p"
        % ", ".join(found))

HDR_FMT = "<HHHHHI"                 # seq, x, y, w, h, len - all little-endian
HDR_LEN = struct.calcsize(HDR_FMT)  # 14, matches the brief exactly


class MirrorDecoder:
    """Reassembles the `>|m=` base64 stream into (seq, x, y, w, h, jpeg) frames.

    Feed it one record's payload (the text after `>|m=`) at a time. Base64
    can only be decoded a whole 4-character group at a time - padding a
    partial group manufactures different bytes rather than recovering its
    prefix, so any leftover 0-3 characters are held over rather than
    guessed at (`kpayload.decode_whole_groups`, added for exactly this).
    Header and payload are then peeled off the resulting byte buffer as
    soon as enough of each has arrived - this stream never pauses for a
    terminator the way a bulk transfer does, so there is nothing to wait
    for.
    """

    def __init__(self):
        self._pending_b64 = ""
        self._buf = b""
        self._header = None   # (seq, x, y, w, h) once parsed, until its len
        self._need = 0        # more bytes complete the frame

    def feed(self, b64_fragment):
        """@return a list of completed (seq, x, y, w, h, jpeg) frames - almost
        always 0 or 1, but a fragment landing right after a short frame can
        complete more than one."""
        from kpayload import decode_whole_groups
        decoded, self._pending_b64 = decode_whole_groups(self._pending_b64,
                                                          b64_fragment)
        if decoded:
            self._buf += decoded
        frames = []
        while True:
            if self._header is None:
                if len(self._buf) < HDR_LEN:
                    break
                seq, x, y, w, h, length = struct.unpack(HDR_FMT,
                                                         self._buf[:HDR_LEN])
                self._buf = self._buf[HDR_LEN:]
                self._header = (seq, x, y, w, h)
                self._need = length
            else:
                if len(self._buf) < self._need:
                    break
                jpeg = self._buf[:self._need]
                self._buf = self._buf[self._need:]
                seq, x, y, w, h = self._header
                frames.append((seq, x, y, w, h, jpeg))
                self._header = None
                self._need = 0
        return frames

    def abort(self):
        """`>|mabort`: the board dropped the frame in progress. Everything
        held so far - including a partial base64 group - has to go, or the
        next feed() would splice a stale fragment onto the next header and
        every frame after it would be read from the wrong offset forever."""
        self._pending_b64 = ""
        self._buf = b""
        self._header = None
        self._need = 0


# A header claiming more JPEG than the firmware's own KMIRROR_JPEG_CAP
# (display_ao.c, 32 KiB) plus its 14-byte header could ever hold is not a
# real frame - see BleFrameDecoder below for why that check exists at all.
BLE_FRAME_CAP = 64 * 1024


class BleFrameDecoder:
    """Reassembles the MIRROR characteristic's raw notify stream into
    (seq, x, y, w, h, jpeg) frames.

    MirrorDecoder's sibling for a wire with no base64 layer at all: BLE
    notifications carry header and JPEG bytes directly, concatenated
    across notifications the same way a frame's bytes are concatenated
    inside one `>|m=` run on the cable - just without the group-of-four
    bookkeeping that only base64 needs. Reusing MirrorDecoder for this
    would mean threading a "there is no base64 here" flag through its one
    job, so this is its own small class instead of that flag.

    There is no abort marker on this wire, and that is deliberate. The
    firmware used to send one as a header carrying len=0, which could never
    be read: an abort is only ever emitted mid-frame, so those fourteen
    bytes land immediately behind a partial body with nothing to separate
    them, and a decoder reading strictly by length consumes them as fourteen
    more JPEG bytes and reads every header after that from the wrong offset.
    The marker was unambiguous only at a frame boundary, which is the one
    place it is never sent. `>|mabort` now arrives on the console
    characteristic instead - framed by newlines, so framing is not the thing
    that has to survive - and `control_line()` calls abort() on both
    decoders alike.

    What is left here is a guard against garbage, not a protocol: a header
    claiming zero bytes or more than the firmware could ever produce is
    corruption, so the fourteen bytes are dropped and the search for a
    header continues rather than stalling forever on a length that will
    never be satisfied.
    """

    def __init__(self):
        self._buf = b""
        self._header = None
        self._need = 0

    def feed(self, chunk):
        self._buf += chunk
        frames = []
        while True:
            if self._header is None:
                if len(self._buf) < HDR_LEN:
                    break
                seq, x, y, w, h, length = struct.unpack(HDR_FMT,
                                                         self._buf[:HDR_LEN])
                self._buf = self._buf[HDR_LEN:]
                if (length == 0) or (length > BLE_FRAME_CAP):
                    continue   # not a frame the board could have sent
                self._header = (seq, x, y, w, h)
                self._need = length
            else:
                if len(self._buf) < self._need:
                    break
                jpeg = self._buf[:self._need]
                self._buf = self._buf[self._need:]
                seq, x, y, w, h = self._header
                frames.append((seq, x, y, w, h, jpeg))
                self._header = None
                self._need = 0
        return frames

    def abort(self):
        """Drop everything held. MirrorDecoder's twin, and it exists because
        the caller does not know which decoder it has: both readers wake a
        paused mirror the same way, and one of them silently had no abort at
        all until a live BLE session found it by dying in the reader thread.
        There is no partial base64 group to clear here - that is the whole
        difference between this class and its sibling - but a half-received
        body and the header that promised it must still go, or the next
        header is read from the middle of the last frame's pixels.
        """
        self._buf = b""
        self._header = None
        self._need = 0


class FrameBus:
    """Where the reader thread posts decoded frames and where each /stream
    connection picks them up.

    Two things are kept: `last_full`, the most recent frame that covered the
    whole canvas - handed to every viewer that connects, so a browser tab
    opened minutes into a session gets a background to paint bands onto
    instead of a black square - and `latest`, whichever frame (full or band)
    arrived most recently, guarded by a Condition so a stream handler can
    block until the next one exists. There is no replay log of the bands
    between `last_full` and now: a viewer that joins late may show a stale
    patch in a region nothing has touched since it connected, and that
    self-heals the moment anything redraws that area. A full per-band log
    would close that gap too, at a memory cost the protocol promises no
    bound on, to fix a staleness window that is normally a couple of
    seconds wide.
    """

    def __init__(self):
        self._cond = threading.Condition()
        self._closed = False
        self._last_full = None    # (seq, x, y, w, h, jpeg)
        self._latest = None
        self._counter = 0

    def publish(self, seq, x, y, w, h, jpeg, canvas_w, canvas_h):
        with self._cond:
            frame = (seq, x, y, w, h, jpeg)
            if (x, y, w, h) == (0, 0, canvas_w, canvas_h):
                self._last_full = frame
            self._latest = frame
            self._counter += 1
            self._cond.notify_all()

    def close(self):
        with self._cond:
            self._closed = True
            self._cond.notify_all()

    @property
    def closed(self):
        return self._closed

    def catch_up(self):
        """(frames to send immediately, counter to wait from) for a viewer
        that just connected."""
        with self._cond:
            frames = [self._last_full] if self._last_full else []
            return frames, self._counter

    def wait_new(self, after_counter, timeout):
        """Block for one frame published after `after_counter`.
        @return (frame, new_counter) or (None, after_counter) on timeout/close."""
        with self._cond:
            end = time.monotonic() + timeout
            while (self._counter == after_counter) and not self._closed:
                remain = end - time.monotonic()
                if remain <= 0:
                    return None, after_counter
                self._cond.wait(remain)
            if self._closed:
                return None, after_counter
            return self._latest, self._counter


class Liveness:
    """When the last frame arrived, and when the host last asked for one.

    The mirror is delta-based, so silence is normally correct and cannot be
    treated as a fault on its own. What is NOT normal is silence after input:
    a press changes the screen, a changed screen produces a frame, so input
    with no frame behind it means the stream has stopped without saying so.
    That is the only condition worth acting on, and it is why both marks are
    kept rather than a single timer.
    """

    def __init__(self):
        self.lock = threading.Lock()
        self.frame = time.monotonic()
        self.input = 0.0

    def saw_frame(self):
        with self.lock:
            self.frame = time.monotonic()

    def sent_input(self):
        with self.lock:
            self.input = time.monotonic()

    def stalled(self, grace):
        """Input landed, nothing came back, and long enough has passed."""
        with self.lock:
            now = time.monotonic()
            return (self.input > self.frame) and ((now - self.input) > grace)


def control_line(line, decoder, board, send_lock, scale, quality, verbose):
    """The two out-of-band records the board sends ABOUT a frame stream.

    Both readers call this on every console line, and that is the point:
    the cable and the BLE transports differ in how frames arrive and not at
    all in how a session is controlled, so a control record handled in one
    reader and forgotten in the other is exactly the drift
    `tools/tests/test_kmirror.py` exists to catch. Neither record carries data, so
    neither needs a decoder that understands the wire it came in on.

    `>|mabort` - the frame in progress is abandoned; everything held has to
    go or the next header is read from the middle of the last frame's
    pixels. It arrives on the console line on BOTH transports: an abort is
    only ever sent mid-frame, so a marker carried in-band on the binary
    characteristic would be indistinguishable from the body bytes it follows
    (display_ao.c, mirror_abort()).

    `>|mpause` - the board put the session to sleep after five seconds with
    nothing moving. There is nothing left outstanding to acknowledge, so the
    only way back is `mirror_start`. This used to be inferred by matching the
    text of a KLOG_W, which `log_level tag="kaos.display" level=e` silences.

    @return True when the line was one of the two.
    """
    if line == ">|mabort":
        decoder.abort()
        if verbose:
            print("kmirror: mabort - board dropped a frame in progress")
        return True
    if line == ">|mpause":
        decoder.abort()
        with send_lock:
            board.send_line(f"mirror_start scale={scale} q={quality}")
        print("kmirror: the board paused the mirror - restarted it")
        return True
    return False


def decode_or_abort(decoder, payload):
    """One record through the decoder, surviving a corrupt one.

    A `>|m=` record reaches this host with characters missing when the OS
    serial buffer overflows mid-frame (kcli.Board.read_line says where that
    comes from), and base64 that is one character past a multiple of four is
    what it looks like from here. That used to raise out of the reader
    thread and kill it: the process kept running and kept answering HTTP, so
    the symptom was "the board froze" and the cure was a restart that reset
    the board and lost the session - four of seven boots in one bench
    session were this (2026-09-15).

    A corrupt frame is recoverable and a dead reader is not. The decoder is
    aborted, which is exactly what `>|mabort` does to it - everything held
    goes, so the next header is read from a header - and the stream carries
    on with one line said about it.

    @return The frames that came out, or an empty list.
    """
    from kpayload import PayloadError

    try:
        return decoder.feed(payload)
    except (PayloadError, binascii.Error, struct.error) as e:
        decoder.abort()
        print(f"kmirror: dropped a corrupt frame record ({e})")
        return []


def reader_loop(board, decoder, bus, canvas_w, canvas_h, send_lock,
                stop_event, verbose, scale=2, quality=70,
                live=None):
    """Runs on its own thread for as long as the process does. The one and
    only reader of `board` - Board's buffer is not locked, so a second
    reader (an HTTP handler, say) would race it for bytes and both would
    lose some. Cable transport; see reader_loop_ble for the BLE twin.

    NOTHING LEAVES THIS THREAD SILENTLY. The whole body is wrapped, because
    a reader that dies takes every board line with it and says so nowhere -
    the one thing this loop must not do is stop without a word.
    """
    try:
        _reader_body(board, decoder, bus, canvas_w, canvas_h, send_lock,
                     stop_event, verbose, scale, quality, live)
    except Exception as e:                       # noqa: BLE001 - see above
        print(f"kmirror: the reader thread stopped: {e!r}")
        stop_event.set()


def _reader_body(board, decoder, bus, canvas_w, canvas_h, send_lock,
                 stop_event, verbose, scale, quality, live):
    while not stop_event.is_set():
        line = board.read_line(time.monotonic() + 0.5)
        if line is None:
            continue
        if line.startswith(">|m="):
            for seq, x, y, w, h, jpeg in decode_or_abort(decoder, line[4:]):
                bus.publish(seq, x, y, w, h, jpeg, canvas_w, canvas_h)
                if live is not None:
                    live.saw_frame()
                with send_lock:
                    board.send_line(f"mirror_ack seq={seq}")
        elif control_line(line, decoder, board, send_lock, scale, quality,
                          verbose):
            # A pause on the cable is usually not a host that went away:
            # control lines and frame records share one wire, so a burst of
            # button presses pushes the acks behind them and the board draws
            # the reasonable conclusion from the evidence it has. Restarting
            # is what control_line() does about it.
            pass
        elif verbose:
            print(f"   {line}")


def reader_loop_ble(board, decoder, bus, canvas_w, canvas_h, send_lock,
                    stop_event, verbose, scale=2, quality=70, live=None):
    """reader_loop's BLE twin: frames arrive as raw bytes from a second
    channel (BleTransport.read_frame(), fed by the MIRROR characteristic)
    instead of as `>|m=` console lines, so this pumps that channel instead
    of read_line() - but still drains whatever console text queued up
    alongside it (the bridge's own status lines, any `!` log kodeOS
    mirrors over the console characteristic) each pass, the same way the
    cable loop's `elif verbose` branch does.

    `board.alive` is checked here, not just by stop_event: read_frame()
    returns None immediately (not after its deadline) once the link is
    gone, on purpose, so a shutdown does not sit out a stale timeout - but
    that same immediacy turns this into a tight spin the instant the BLE
    link drops out from under a live session (a board reset while
    streaming, say), with nothing left to sleep on. Ending the thread
    once `alive` goes false is what a cable Board never had to do: its
    read_line() always paces itself against the deadline even after the
    port errors out, because it never short-circuits on a liveness flag
    the way this one does.

    Wrapped like the cable twin, for the cable twin's reason: a reader that
    ends without a word is the one failure this loop must not have."""
    try:
        _reader_body_ble(board, decoder, bus, canvas_w, canvas_h, send_lock,
                         stop_event, verbose, scale, quality, live)
    except Exception as e:                       # noqa: BLE001 - see above
        print(f"kmirror: the reader thread stopped: {e!r}")
        stop_event.set()


def _reader_body_ble(board, decoder, bus, canvas_w, canvas_h, send_lock,
                     stop_event, verbose, scale, quality, live):
    while (not stop_event.is_set()) and board.alive:
        chunk = board.read_frame(time.monotonic() + 0.5)
        for line in board.take_lines():
            # Frames have a characteristic of their own here, so they do not
            # contend with commands the way the cable's single wire does -
            # but the control records still arrive on the console pipe, and
            # they are the same two records with the same meaning. Over BLE
            # `>|mabort` is the ONLY way a truncated frame can be announced
            # at all: the binary pipe is framed by length, so a marker inside
            # it cannot be told from the body it follows.
            if control_line(line, decoder, board, send_lock, scale, quality,
                            verbose):
                pass
            elif verbose:
                print(f"   {line}")
        if chunk is None:
            continue
        for seq, x, y, w, h, jpeg in decode_or_abort(decoder, chunk):
            bus.publish(seq, x, y, w, h, jpeg, canvas_w, canvas_h)
            if live is not None:
                live.saw_frame()
            with send_lock:
                board.send_line(f"mirror_ack seq={seq}")


def die(msg):
    sys.exit(f"kmirror: {msg}")


def make_page(canvas_w, canvas_h, scale):
    """The one HTML page this server has: a canvas, a stream reader that
    composites kodeOS's own header+JPEG framing onto it, and a click
    handler that turns a canvas-pixel coordinate into a `tap`."""
    return f"""<!doctype html>
<html><head><meta charset="utf-8">
<title>kodeOS mirror</title>
<style>
  body {{ margin: 0; background: #111; color: #ccc; font: 13px monospace;
         display: flex; flex-direction: column; align-items: center; }}
  /* Bounded by HEIGHT, not just width. Sized only by width the picture grew
     until the pad underneath it fell off the bottom of the window, and the
     buttons were then invisible without scrolling - which reads exactly like
     buttons that were never drawn. The aspect ratio keeps the width honest. */
  canvas {{ image-rendering: pixelated; background: #000; margin-top: 10px;
           max-width: min(90vmin, {canvas_w * 3}px);
           max-height: 62vh; width: auto; height: auto;
           aspect-ratio: {canvas_w} / {canvas_h};
           cursor: crosshair; }}
  #bar {{ margin: 8px; }}
  button {{ font: inherit; }}
  /* The pad mirrors the board: a four-way cross for the D-pad, and the two
     individual buttons either side of it, where the thumbs actually find
     them. Nothing here is a screenshot of the hardware - it is a map of it,
     so a stranger can drive the board without one in front of them. */
  /* Named areas rather than counted cells: the shape IS the point here, and
     a layout spelled as a picture cannot drift out of step with itself the
     way a run of invisible spacer cells can. */
  #pad {{ display: grid; gap: 8px; margin: 16px 0 6px;
          grid-template-columns: 54px 54px 54px 18px 66px;
          grid-template-rows: 44px 44px 44px;
          grid-template-areas: ".  up .  . tp"
                               "lf .  rt . . "
                               ".  dn .  . bt"; }}
  #pad button {{ background: #222; color: #ddd; border: 1px solid #3a3a3a;
                 border-radius: 8px; cursor: pointer; font-size: 17px;
                 user-select: none; -webkit-user-select: none;
                 touch-action: none; }}
  #pad button:hover {{ border-color: #555; }}
  #pad button:active, #pad button.held {{ background: #2ea3f2; color: #06263b;
                                          border-color: #2ea3f2; }}
  #pad .side {{ font-size: 12px; letter-spacing: 1px; }}
  #hint {{ color: #777; margin-bottom: 18px; }}
</style></head>
<body>
<div id="bar">kodeOS mirror - scale {scale} -
  <span id="status">connecting...</span> -
  <button id="full">fullscreen</button></div>
<canvas id="c" width="{canvas_w}" height="{canvas_h}"></canvas>
<div id="pad">
  <button data-id="up"    style="grid-area:up">&#9650;</button>
  <button data-id="left"  style="grid-area:lf">&#9664;</button>
  <button data-id="right" style="grid-area:rt">&#9654;</button>
  <button data-id="down"  style="grid-area:dn">&#9660;</button>
  <button data-id="top"   style="grid-area:tp" class="side">TOP</button>
  <button data-id="bot"   style="grid-area:bt" class="side">BOT</button>
</div>
<div id="hint">click the screen to tap &middot; arrows / enter / esc drive the pad</div>
<script>
const HDR = 14;             // seq u16, x u16, y u16, w u16, h u16, len u32
const canvas = document.getElementById('c');
const ctx = canvas.getContext('2d');
const status = document.getElementById('status');
let buf = new Uint8Array(0);
let frames = 0;

document.getElementById('full').onclick = () => canvas.requestFullscreen();

canvas.addEventListener('click', (ev) => {{
  const rect = canvas.getBoundingClientRect();
  const cx = Math.round((ev.clientX - rect.left) * (canvas.width / rect.width));
  const cy = Math.round((ev.clientY - rect.top) * (canvas.height / rect.height));
  fetch('/tap?x=' + cx + '&y=' + cy, {{ method: 'POST' }}).catch(() => {{}});
}});

// Press and release are sent separately rather than as one "click", because
// that is what the board's own buttons produce and the GUI is free to care
// about the difference - an arm-then-confirm control does. Holding the mouse
// down holds the button, for the same reason.
const held = new Set();
function btn(id, state) {{
  if (state === 'press') {{
    if (held.has(id)) return;     // autorepeat would otherwise machine-gun it
    held.add(id);
  }} else {{
    if (!held.delete(id)) return; // never release what was never pressed
  }}
  const el = document.querySelector('[data-id="' + id + '"]');
  if (el) el.classList.toggle('held', state === 'press');
  fetch('/btn?id=' + id + '&state=' + state, {{ method: 'POST' }}).catch(() => {{}});
}}

for (const el of document.querySelectorAll('#pad button')) {{
  const id = el.dataset.id;
  el.addEventListener('pointerdown', (e) => {{ e.preventDefault(); btn(id, 'press'); }});
  // pointerup on the window, not the button: a drag that leaves the button
  // before releasing would otherwise leave the board holding it forever.
  el.addEventListener('pointerleave', () => btn(id, 'release'));
  el.addEventListener('contextmenu', (e) => e.preventDefault());
}}
window.addEventListener('pointerup', () => {{
  for (const id of Array.from(held)) btn(id, 'release');
}});

const KEYS = {{ ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left',
                ArrowRight: 'right', Enter: 'top', Escape: 'bot' }};
window.addEventListener('keydown', (e) => {{
  const id = KEYS[e.key];
  if (!id) return;
  e.preventDefault();
  btn(id, 'press');
}});
window.addEventListener('keyup', (e) => {{
  const id = KEYS[e.key];
  if (id) btn(id, 'release');
}});
// A tab that loses focus mid-press must not leave a finger down on the board.
window.addEventListener('blur', () => {{
  for (const id of Array.from(held)) btn(id, 'release');
}});

function append(chunk) {{
  const merged = new Uint8Array(buf.length + chunk.length);
  merged.set(buf);
  merged.set(chunk, buf.length);
  buf = merged;
}}

function pump() {{
  while (true) {{
    if (buf.length < HDR) return;
    const dv = new DataView(buf.buffer, buf.byteOffset, HDR);
    const seq = dv.getUint16(0, true);
    const x = dv.getUint16(2, true);
    const y = dv.getUint16(4, true);
    const len = dv.getUint32(10, true);
    if (buf.length < HDR + len) return;
    const jpeg = buf.slice(HDR, HDR + len);
    buf = buf.slice(HDR + len);
    frames++;
    status.textContent = 'frame ' + frames + ' (seq ' + seq + ')';
    createImageBitmap(new Blob([jpeg], {{ type: 'image/jpeg' }}))
      .then((bmp) => {{ ctx.drawImage(bmp, x, y); bmp.close(); }})
      .catch(() => {{}});
  }}
}}

async function run() {{
  const resp = await fetch('/stream');
  const reader = resp.body.getReader();
  for (;;) {{
    const {{ done, value }} = await reader.read();
    if (done) {{ status.textContent = 'stream ended'; return; }}
    append(value);
    pump();
  }}
}}
run().catch((e) => {{ status.textContent = 'stream error: ' + e; }});
</script>
</body></html>
"""


def make_handler(bus, board, send_lock, scale, page, live=None):
    """A bound Handler class - the standard http.server way to hand a
    request handler some state without module-level globals."""

    class Handler(http.server.BaseHTTPRequestHandler):
        server_version = "kmirror/1"
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *args):
            pass   # the console records already say what happened

        def do_GET(self):
            parsed = urllib.parse.urlparse(self.path)
            if parsed.path == "/":
                body = page.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                # The page changes with the tool, and a browser that cached an
                # older one shows a viewer missing whatever was added since -
                # which looks like a broken feature rather than a stale tab.
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif parsed.path == "/stream":
                self._stream()
            else:
                self.send_error(404)

        def do_POST(self):
            parsed = urllib.parse.urlparse(self.path)
            if parsed.path == "/tap":
                self._tap(urllib.parse.parse_qs(parsed.query))
            elif parsed.path == "/btn":
                self._btn(urllib.parse.parse_qs(parsed.query))
            elif parsed.path == "/touch":
                self._touch(urllib.parse.parse_qs(parsed.query))
            else:
                self.send_error(404)

        def _touch(self, params):
            """One raw touch sample: down, move or up, in canvas pixels.

            What `/tap` cannot express - a finger that travels. A swipe is a
            `down`, any number of `move`s and an `up`, each its own request,
            which is how a script drives the glass the way a hand does. The
            board refuses a sample while a tap's release is still pending;
            that answer goes to the log like every other line on this port.
            """
            state = (params.get("state") or [""])[0]
            try:
                cx = float(params["x"][0])
                cy = float(params["y"][0])
            except (KeyError, ValueError, IndexError):
                self.send_error(400, "need x=, y= (canvas pixel coords) and "
                                     "state=<down|move|up>")
                return
            if state not in ("down", "move", "up"):
                self.send_error(400, "need state=<down|move|up>")
                return
            px = max(0, min(PANEL_W - 1, round(cx * scale)))
            py = max(0, min(PANEL_H - 1, round(cy * scale)))
            with send_lock:
                board.send_line(f"touch x={px} y={py} state={state}")
            if live is not None:
                live.sent_input()
            self._ok()

        def _stream(self):
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.end_headers()
            frames, counter = bus.catch_up()
            try:
                for f in frames:
                    self._write_frame(f)
                while True:
                    frame, counter = bus.wait_new(counter, timeout=15.0)
                    if frame is not None:
                        self._write_frame(frame)
                    elif bus.closed:
                        break   # shutting down - stop rather than spin
            except (BrokenPipeError, ConnectionResetError):
                pass   # the tab closed or navigated away; nothing to clean up

        def _write_frame(self, frame):
            seq, x, y, w, h, jpeg = frame
            self.wfile.write(struct.pack(HDR_FMT, seq, x, y, w, h, len(jpeg)))
            self.wfile.write(jpeg)
            self.wfile.flush()

        def _tap(self, params):
            try:
                cx = float(params["x"][0])
                cy = float(params["y"][0])
            except (KeyError, ValueError, IndexError):
                self.send_error(400, "need x= and y= (canvas pixel coords)")
                return
            # The browser only ever sees the scaled-down canvas; the board
            # wants physical panel coordinates.
            px = max(0, min(PANEL_W - 1, round(cx * scale)))
            py = max(0, min(PANEL_H - 1, round(cy * scale)))
            with send_lock:
                board.send_line(f"tap x={px} y={py}")
            if live is not None:
                live.sent_input()
            self._ok()

        def _btn(self, params):
            """One button edge, forwarded verbatim.

            The id is checked against BUTTON_IDS here rather than passed
            through, because everything on this port is a line the board
            parses: a query string that arrived with a newline in it would
            otherwise become a second command nobody typed. The board would
            refuse the malformed verb, but the injection is the part worth
            not writing.
            """
            bid = (params.get("id") or [""])[0]
            state = (params.get("state") or [""])[0]
            if bid not in BUTTON_IDS or state not in ("press", "release"):
                self.send_error(400, "need id=<up|down|left|right|top|bot> "
                                     "and state=<press|release>")
                return
            with send_lock:
                board.send_line(f"btn id={bid} state={state}")
            if live is not None:
                live.sent_input()
            self._ok()

        def _ok(self):
            body = b"ok"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    return Handler


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


# ---- The BLE transport -----------------------------------------------------
#
# Everything above this point only ever calls read_line/send_line/command/
# drain/follow/close on whatever "board" main() built - the same five verbs
# kcli.py's Board offers over the cable. BleTransport below is a second
# class with that same shape, so none of it needs to know which wire it is
# on; read_frame()/take_lines() are the two extra verbs only reader_loop_ble
# calls, because BLE's mirror frames arrive on a channel the cable protocol
# has no equivalent of.
#
# The board itself never speaks bleak: a plain python process on this Mac
# has no Bluetooth TCC permission (BleakScanner.discover() returns nothing
# at all - not an error, the process just never sees a result), so the
# actual radio work happens inside a small signed .app under the cache,
# built by kblebundle.py the same way kble.py's bridge is. That bridge is
# THIS FILE, re-executed with --ble-bridge inside the bundle (a copy of it,
# for the reason kblebundle.ensure_bundle's docstring gives); the two
# halves talk over a Unix socket, tagged so a console text line and a raw
# MIRROR notification can share it: one byte of tag (b"T" or b"M"), four
# bytes of little-endian length, then that many bytes.
#
# Unlike kble.py's bridge, this one does not hold the link across separate
# command invocations - kmirror.py IS the long-running session (a server a
# person Ctrl-Cs), so there is nothing to hold it FOR between commands.
# One bridge, one client, exits when either side goes away. That drops
# kble.py's whole hello/version/busy negotiation, which existed to let a
# bridge from a previous version step aside for a newer one launched by a
# later command - a problem that only exists if the bridge outlives the
# process that started it.

BLE_SVC_UUID = "4b4f4445-0001-4f53-a2d1-7c0e9b5f3a61"
BLE_RX_UUID = "4b4f4445-0002-4f53-a2d1-7c0e9b5f3a61"
BLE_TX_UUID = "4b4f4445-0003-4f53-a2d1-7c0e9b5f3a61"
BLE_MIRROR_UUID = "4b4f4445-0004-4f53-a2d1-7c0e9b5f3a61"

BLE_CACHE = os.path.expanduser("~/Library/Caches/kode-kmirror")
BLE_APP = os.path.join(BLE_CACHE, "KodeMirror.app")
BLE_LOG = os.path.join(BLE_CACHE, "bridge.log")
BLE_SCRIPT_COPY = os.path.join(BLE_CACHE, "kmirror_bridge.py")
BLE_SOCK = os.path.join(BLE_CACHE, "bridge.sock")


def _send_tagged(writer, tag, payload):
    writer.write(tag + len(payload).to_bytes(4, "little") + payload)


async def _ble_write(state, data):
    """Write to RX, chunked to whatever window _ble_bridge_serve measured
    off the connection's own MTU - the same chunking kble.py's
    BleakRadio.write() does, and for the same reason: a write bigger than
    the link's MTU is simply refused, not queued or split for you."""
    client = state["client"]
    window = state["window"]
    pending = bytes(data)
    while pending:
        take, pending = pending[:window], pending[window:]
        await client.write_gatt_char(BLE_RX_UUID, take, response=False)


async def _ble_pair(client, say):
    """Provoke CoreBluetooth into pairing before anything needs the
    encrypted link - kble.py's BleakRadio._pair() has the full account of
    why a WRITE and not a subscribe is what actually triggers it on a Mac,
    and why the first attempt is expected to fail. Duplicated rather than
    imported: this bridge has no dependency on kble.py at all, and the
    six-line retry loop is cheaper to repeat than to couple two otherwise
    unrelated tools over."""
    last = None
    for _ in range(6):
        try:
            await client.write_gatt_char(BLE_RX_UUID, b"\n", response=True)
            return True
        except Exception as exc:              # noqa: BLE001 - any refusal
            last = exc
            await asyncio.sleep(1.0)
    say(f"failed to pair: {last}")
    return False


async def _ble_bridge_serve(sock_path, timeout, verbose):
    """The half that actually touches Bluetooth - runs only inside the
    signed bundle.

    Scans, connects and subscribes BEFORE the Unix socket is even bound -
    every `say()`/on_tx/on_mirror event before that point is queued rather
    than sent, and flushed once a client actually connects. That ordering
    is not cosmetic: with the socket server started first (bound, one
    client accepted, THEN scan), find_device_by_filter() reliably burned
    its whole timeout on this bench and returned no device - twice,
    back to back - while the exact same scan with no server anywhere in
    the process found the board in 1.7-4.1s every time tried. Scan-first,
    server-after made the scan reliable again on the same runs; whatever
    the actual interaction is between asyncio.start_unix_server() and
    bleak's CoreBluetooth backend sharing one event loop on this Python
    (3.14, python.org build) is not chased further here - not our bug to
    fix, only one to not collide with. The cost is that a client that
    dials in while this is scanning sees nothing until the scan and
    connect are both done, rather than watching `found`/`attached` land
    live; queueing is what makes that a wait instead of a lost message.

    Single-shot: this bridge serves exactly one client for exactly one
    BLE session and then is done, unlike kble.py's, which outlives the
    command that starts it. See the module-level comment above
    BLE_SVC_UUID for why that difference is not a shortcut.
    """
    from bleak import BleakClient, BleakScanner

    try:
        os.unlink(sock_path)
    except FileNotFoundError:
        pass

    loop = asyncio.get_running_loop()
    done = loop.create_future()
    first_client = loop.create_future()
    state = {"writer": None, "client": None, "window": 20, "pending": []}

    def end(why):
        if not done.done():
            done.set_result(why)

    def emit(tag, payload):
        w = state["writer"]
        if w is None:
            state["pending"].append((tag, payload))
        elif not w.is_closing():
            _send_tagged(w, tag, payload)

    def say(msg):
        emit(b"T", ("\x00kmirror: " + msg + "\n").encode())

    def on_tx(_h, data):
        emit(b"T", bytes(data))

    def on_mirror(_h, data):
        emit(b"M", bytes(data))

    def on_drop(_client):
        end("dropped")

    async def serve(reader, writer):
        if state["writer"] is not None:
            writer.close()          # single-shot: one client at a time
            return
        state["writer"] = writer
        pending, state["pending"] = state["pending"], []
        for tag, payload in pending:
            if not writer.is_closing():
                _send_tagged(writer, tag, payload)
        if not first_client.done():
            first_client.set_result(True)
        try:
            while True:
                data = await reader.read(4096)
                if not data:
                    break
                try:
                    await _ble_write(state, data)
                except Exception as e:   # whatever bleak raises: link is gone
                    say(f"the link dropped: {e}")
                    end("dropped")
                    break
        finally:
            if state["writer"] is writer:
                state["writer"] = None
            writer.close()
            end("host gone")

    try:
        dev = await BleakScanner.find_device_by_filter(
            lambda d, ad: BLE_SVC_UUID.lower()
                        in [u.lower() for u in (ad.service_uuids or [])],
            timeout=timeout)
        if dev is None:
            say("no board is advertising the mirror service - run "
                "`ble_up` on the cable, or check nothing else is already "
                "attached (only one BLE central at a time)")
            end("no board")
        else:
            say(f"found {dev.name or '?'} ({dev.address})")
            client = BleakClient(dev, disconnected_callback=on_drop)
            state["client"] = client
            await client.connect()
            try:
                await client.start_notify(BLE_TX_UUID, on_tx)
                await client.start_notify(BLE_MIRROR_UUID, on_mirror)
            except Exception:                  # noqa: BLE001 - any refusal
                if not await _ble_pair(client, say):
                    end("failed")
                else:
                    await client.start_notify(BLE_TX_UUID, on_tx)
                    await client.start_notify(BLE_MIRROR_UUID, on_mirror)
            if not done.done():
                mtu = getattr(client, "mtu_size", 0) or 23
                state["window"] = max(mtu - 3, 20)
                say("attached")
    except Exception as e:                     # a connect that failed
        import traceback
        traceback.print_exc()
        say(f"failed to attach: {e}")
        end("failed")

    server = await asyncio.start_unix_server(serve, path=sock_path)
    try:
        # Give the host (already retrying its own dial in a loop) a chance
        # to connect and receive whatever is queued - `done` may already
        # be resolved (the no-board/failed paths above) and awaiting it
        # directly would tear the server down before anyone ever read why.
        await asyncio.wait_for(first_client, timeout=15.0)
    except asyncio.TimeoutError:
        pass          # nobody ever connected; nothing left to flush to

    why = await done
    server.close()
    if state["writer"] is not None:
        state["writer"].close()
    await server.wait_closed()
    client = state["client"]
    try:
        if (client is not None) and client.is_connected:
            await client.disconnect()
    except Exception:
        pass
    try:
        os.unlink(sock_path)
    except OSError:
        pass
    print(f"bridge: {why}")   # into bridge.log


def _launch_ble_bridge(timeout):
    """Build (if needed) and start the signed bundle that runs THIS FILE
    with --ble-bridge. macOS only - kble.py's docstring covers why no
    other platform needs any of this scaffolding."""
    import kblebundle
    # "a python that can import bleak" has no opinion about which tool is
    # asking, so this deliberately points at kble.py's own cache rather
    # than building a second copy: if kble.py already paid the one-time
    # ~20s venv cost, reusing it costs nothing and risks nothing (the venv
    # holds no protocol code, only the interpreter and the package). The
    # bundle itself stays separate - see the module comment above
    # BLE_SVC_UUID for why that part is NOT shared.
    py = kblebundle.python_with_bleak(
        os.path.expanduser("~/Library/Caches/kode-kble"))
    if sys.platform == "darwin":
        kblebundle.ensure_bundle(
            app_dir=BLE_APP, exe_name="KodeMirror", bundle_id="diy.kode.kmirror",
            display_name="Kode mirror (BLE)",
            usage_text="Streams a Kode Dot's panel mirror over BLE.",
            py=py, script_src=os.path.abspath(__file__),
            script_copy=BLE_SCRIPT_COPY, log_path=BLE_LOG)
        kblebundle.launch(BLE_APP, ["--ble-bridge", "--sock", BLE_SOCK,
                                    "--ble-timeout", str(timeout)])
    else:
        os.makedirs(BLE_CACHE, exist_ok=True)
        with open(BLE_LOG, "ab") as log:
            subprocess.Popen([py, os.path.abspath(__file__), "--ble-bridge",
                              "--sock", BLE_SOCK, "--ble-timeout", str(timeout)],
                             stdout=log, stderr=log, start_new_session=True)


class BleTransport:
    """A Board look-alike (read_line/send_line/command/drain/follow/close)
    backed by the bridge above, plus read_frame()/take_lines() for the
    binary MIRROR channel a serial Board never had to offer. Everything
    that only calls the five verbs it shares with Board - main()'s
    mirror_start/mirror_stop, the HTTP handler's /tap - works against this
    exactly as it does against a cable, unmodified.
    """

    def __init__(self, timeout=25.0, verbose=False):
        self.verbose = verbose
        self._lock = threading.Lock()
        self._text_buf = ""
        self._lines = []
        self._status = []
        self._frames = []
        self.alive = True
        os.makedirs(BLE_CACHE, exist_ok=True)
        try:
            os.unlink(BLE_SOCK)
        except FileNotFoundError:
            pass
        _launch_ble_bridge(timeout)
        self.sock = self._dial(timeout)
        threading.Thread(target=self._reader, daemon=True).start()

    def _dial(self, timeout):
        end = time.time() + timeout + 15.0
        while time.time() < end:
            try:
                s = socket.socket(socket.AF_UNIX)
                s.settimeout(2.0)
                s.connect(BLE_SOCK)
                s.settimeout(None)
                return s
            except OSError:
                time.sleep(0.2)
        hint = "\n  a bridge may be stuck: `pkill -f 'kmirror.*--ble-bridge'`"
        if os.path.exists(BLE_LOG):
            with open(BLE_LOG) as f:
                tail = f.read().strip().splitlines()[-6:]
            hint = "\n  " + "\n  ".join(tail) + hint
        die("the BLE bridge never came up" + hint)

    def _reader(self):
        buf = b""
        while self.alive:
            try:
                chunk = self.sock.recv(65536)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while len(buf) >= 5:
                tag = buf[0:1]
                length = int.from_bytes(buf[1:5], "little")
                if len(buf) < (5 + length):
                    break
                payload = buf[5:5 + length]
                buf = buf[5 + length:]
                if tag == b"T":
                    with self._lock:
                        self._text_buf += payload.decode("utf-8", "replace")
                        while "\n" in self._text_buf:
                            line, self._text_buf = self._text_buf.split("\n", 1)
                            line = line.rstrip("\r")
                            if line.startswith("\x00kmirror: "):
                                self._status.append(line[1:])
                            elif line:
                                self._lines.append(line)
                elif tag == b"M":
                    with self._lock:
                        self._frames.append(payload)
        self.alive = False

    def wait_status(self, pred, timeout):
        """Collect bridge status lines (attach progress, pairing, drops)
        until one matches `pred`. Returns (all seen, the matching one or
        None on timeout/close) - the BLE equivalent of watching a cable
        Board's boot log for a line that means something."""
        end = time.monotonic() + timeout
        seen = []
        while time.monotonic() < end:
            with self._lock:
                got, self._status = self._status, []
            for s in got:
                seen.append(s)
                if pred(s):
                    return seen, s
            if not self.alive:
                break
            time.sleep(0.05)
        return seen, None

    def take_lines(self):
        """All console text lines queued since the last call, in order -
        used by reader_loop_ble to drain log/status chatter that arrives
        interleaved with mirror frames, without blocking on it."""
        with self._lock:
            out, self._lines = self._lines, []
        return out

    def read_line(self, deadline):
        while True:
            with self._lock:
                if self._lines:
                    return self._lines.pop(0)
            if time.monotonic() >= deadline:
                return None
            if not self.alive:
                return None
            time.sleep(0.02)

    def read_frame(self, deadline):
        while True:
            with self._lock:
                if self._frames:
                    return self._frames.pop(0)
            if time.monotonic() >= deadline:
                return None
            if not self.alive:
                return None
            time.sleep(0.01)

    def send_line(self, line):
        if self.verbose:
            print(f"<- {line}")
        self.sock.sendall((line + "\n").encode())

    def command(self, line, timeout=5.0):
        """Same contract as Board.command(): send one line, collect `>|`
        records until a terminator, return (records, terminator)."""
        self.send_line(line)
        deadline = time.monotonic() + timeout
        records = []
        while True:
            got = self.read_line(deadline)
            if got is None:
                return records, None
            if got.startswith(">|"):
                records.append(got[2:])
            elif got.startswith(">ok") or got.startswith(">err"):
                return records, got
            elif self.verbose:
                print(f"   {got}")

    def drain(self, seconds=0.3):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.read_line(min(deadline, time.monotonic() + 0.05))
        with self._lock:
            self._lines = []
            self._frames = []

    def follow(self, seconds):
        deadline = time.monotonic() + seconds
        while True:
            line = self.read_line(deadline)
            if line is None:
                return
            print(f"   {line}")

    def close(self):
        """shutdown() before close(), the same way kble.py's Link does and
        for the same reason: _reader() is blocked in recv() on this socket
        from another thread, and on Linux close() frees the descriptor while
        that blocked call keeps the connection itself alive. The bridge -
        single-shot, one client at a time - would then hold this client's
        slot against the next command. macOS tears it down anyway, which is
        the only reason this ever looked fine."""
        self.alive = False
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-p", "--port",
                    help="serial port (cable transport). Omit it and the "
                         "board is found automatically - the port number "
                         "changes on almost every reset, so a written-down "
                         "one is usually the stale one")
    ap.add_argument("--ble", action="store_true",
                    help="stream over BLE instead of the cable - builds a "
                         "small signed .app under ~/Library/Caches the "
                         "first time it runs and macOS will prompt for "
                         "Bluetooth permission once; only one BLE central "
                         "may be attached to the board at a time")
    ap.add_argument("--ble-timeout", type=float, default=25.0, metavar="S",
                    help="how long to scan for the board over BLE "
                         "(default 25)")
    ap.add_argument("--ble-bridge", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--sock", default=BLE_SOCK, help=argparse.SUPPRESS)
    ap.add_argument("--scale", type=int, choices=(1, 2), default=2,
                    help="mirror_start scale= (default 2)")
    ap.add_argument("--q", type=int, default=70, metavar="1-100",
                    help="JPEG quality, mirror_start q= (default 70)")
    ap.add_argument("--host", default="127.0.0.1", help="HTTP bind address")
    ap.add_argument("--http-port", type=int, default=8420,
                    help="HTTP port to serve the viewer on (default 8420)")
    ap.add_argument("--open", action="store_true",
                    help="open the viewer in the default browser")
    ap.add_argument("--steal", action="store_true",
                    help="if the serial port is busy, terminate whoever "
                         "holds it and retry (cable transport only)")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="show board log lines while streaming")
    args = ap.parse_args()

    if args.ble_bridge:
        # We are the half inside the bundle - see _ble_bridge_serve's own
        # docstring. Nothing past this point (HTTP server, FrameBus, the
        # cable Board) is reachable from here; this returns once the one
        # client it serves goes away.
        asyncio.run(_ble_bridge_serve(args.sock, args.ble_timeout, args.verbose))
        return

    if args.ble and args.port:
        die("choose one of -p / --ble, not both")
    if (not args.ble) and (not args.port):
        # Not an error any more: the port is discoverable and writing one
        # down is how these tasks go stale between one reset and the next.
        args.port = find_port()
        print(f"kmirror: found the board at {args.port}")
    if not (1 <= args.q <= 100):
        die("--q must be 1..100")

    canvas_w, canvas_h = PANEL_W // args.scale, PANEL_H // args.scale

    if args.ble:
        board = BleTransport(timeout=args.ble_timeout, verbose=args.verbose)
        seen, hit = board.wait_status(
            lambda s: ("attached" in s) or ("no board" in s)
                    or ("failed" in s) or ("dropped" in s),
            args.ble_timeout + 15.0)
        for s in seen:
            print(f"kmirror: {s}")
        if (hit is None) or ("attached" not in hit):
            board.close()
            die("could not attach over BLE" +
               (f": {hit}" if hit else " (the bridge closed with no reason given)"))
        board.drain()
        decoder = BleFrameDecoder()
        reader_fn = reader_loop_ble
    else:
        from kcli import Board
        board = Board(args.port, verbose=args.verbose, steal=args.steal)
        board.drain()
        # Opening this port RESETS the board - the USB-Serial/JTAG peripheral
        # does it on attach and clearing DTR/RTS first does not prevent it -
        # so the first thing on the wire is a boot log, not a console. Sending
        # mirror_start into that gets it swallowed, and the symptom is a
        # viewer that connects, says nothing is wrong, and shows a black
        # square forever. Wait for the console to actually answer instead of
        # guessing at a delay: a board that is up replies to ping at once,
        # and one that is still bringing the panel round takes about five
        # seconds.
        if not wait_ready(board, 20.0, verbose=args.verbose):
            die("the board never answered ping - is it running kodeOS?")
        decoder = MirrorDecoder()
        reader_fn = reader_loop

    records, term = board.command(f"mirror_start scale={args.scale} q={args.q}",
                                  timeout=5.0)
    for r in records:
        print(f"kmirror: {r}")
    if term is None or not term.startswith(">ok"):
        board.close()
        die(f"mirror_start: {term or 'no response (timeout)'}")

    bus = FrameBus()
    live = Liveness()
    send_lock = threading.Lock()
    stop_event = threading.Event()
    reader = threading.Thread(target=reader_fn,
                              args=(board, decoder, bus, canvas_w, canvas_h,
                                    send_lock, stop_event, args.verbose,
                                    args.scale, args.q, live),
                              daemon=True)
    reader.start()

    page = make_page(canvas_w, canvas_h, args.scale)
    def rearm_watch():
        """Re-arm a stream that went quiet after input.

        The board pauses a mirror nobody acknowledges after five seconds,
        and that is right - it should not hold the JPEG engine for a host
        that has gone away. On the cable, though, an ack is not late because
        the host went away: control lines and frame records share one wire,
        so a burst of presses pushes the acks behind it and the board draws
        the reasonable conclusion from the evidence it has. Waiting for the
        `>|mpause` record works but costs the full five seconds of frozen
        picture; this notices the same thing sooner, from the side that
        knows an input was sent and no picture came back.

        decoder.abort() first, and it is not optional. A restart landing on
        a frame that is still going out makes the board abandon it, so the
        bytes already received promise a `len` that will never be completed.
        The board says so with `>|mabort` and the reader clears the decoder
        on that - but the restart is issued from HERE, on another thread, and
        clearing locally as well is what keeps the two from racing over which
        bytes belong to which frame. Clearing an already-empty decoder costs
        nothing.
        """
        while not stop_event.wait(0.5):
            if live.stalled(REARM_AFTER):
                decoder.abort()
                with send_lock:
                    board.send_line(f"mirror_start scale={args.scale} "
                                    f"q={args.q}")
                live.saw_frame()      # give the restart its own grace period
                if args.verbose:
                    print("kmirror: no frame after input - restarted the mirror")

    threading.Thread(target=rearm_watch, daemon=True).start()

    handler = make_handler(bus, board, send_lock, args.scale, page, live)
    httpd = Server((args.host, args.http_port), handler)
    url = f"http://{args.host}:{args.http_port}/"
    transport_note = "BLE" if args.ble else args.port
    print(f"kmirror: {canvas_w}x{canvas_h} at scale={args.scale} q={args.q} "
         f"over {transport_note} - serving {url}")

    if args.open:
        import webbrowser
        webbrowser.open(url)

    try:
        httpd.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        print("\nkmirror: stopping")
        stop_event.set()
        reader.join(timeout=2.0)
        bus.close()
        httpd.shutdown()
        httpd.server_close()
        with send_lock:
            board.send_line("mirror_stop")
        board.follow(0.5)
        board.close()


if __name__ == "__main__":
    main()
