#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What the two transports must keep in common, checked without a board.

kmirror carries the same frames over a cable and over BLE, and the two
halves are deliberately separate: different readers, different decoders,
because the wires genuinely differ - one is base64 inside console lines,
the other is raw bytes on a characteristic of its own. What they must not
do is DRIFT, and twice they did.

Both times it cost a live session to notice. A parameter added to the
cable reader was not added to its BLE twin, so the BLE thread died with a
TypeError the moment it started - and the picture simply never appeared,
with nothing on the terminal unless somebody thought to look. Then the
wake-up path called abort() on a decoder that had never had one, and the
thread died again, at the one moment the code existed to handle.

Neither is the sort of thing a reader spots: both halves look right on
their own. So this pins the shape they share rather than the behaviour
they do not.

    usage: python3 tools/tests/test_kmirror.py
    exit:  0 all good, 1 something drifted
"""
import base64
import inspect
import os
import struct
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import kmirror

FAILURES = []


def check(name, ok, detail=""):
    print(f"  {'ok  ' if ok else 'FAIL'}  {name}{(' - ' + detail) if detail else ''}")
    if not ok:
        FAILURES.append(name)


def main():
    print("kmirror: the two transports")

    # The readers are chosen at runtime and handed ONE args tuple, so their
    # signatures are not merely similar - they have to be interchangeable.
    cable = list(inspect.signature(kmirror.reader_loop).parameters)
    ble = list(inspect.signature(kmirror.reader_loop_ble).parameters)
    check("both readers take the same parameters, in the same order",
          cable == ble, f"cable={cable} ble={ble}")

    # Both decoders are driven by the same call sites: feed() every pass and
    # abort() when the board says it dropped a frame or paused.
    for cls in (kmirror.MirrorDecoder, kmirror.BleFrameDecoder):
        for method in ("feed", "abort"):
            check(f"{cls.__name__}.{method}() exists",
                  callable(getattr(cls, method, None)))

    # And abort() has to actually clear, or the next header is read out of
    # the middle of the last frame's pixels.
    for cls in (kmirror.MirrorDecoder, kmirror.BleFrameDecoder):
        d = cls()
        d.feed("QUJD" if cls is kmirror.MirrorDecoder else b"\x01\x02\x03")
        d.abort()
        check(f"{cls.__name__}.abort() empties the buffer", d._buf == b"")

    # ---- The golden header ------------------------------------------------
    # The firmware writes this format in C (kmirror_proto.c) and this file
    # reads it in Python. Both have tests; both round-trip an implementation
    # against ITSELF, so both stay green while the two disagree - and what
    # breaks then is a board talking to a viewer, which is the one failure
    # neither suite can see on its own.
    #
    # These fourteen bytes are the third party neither side can move. The same
    # literal is hand-written in sdk/tests/host/cases/test_kmirror_proto.c,
    # and the duplication IS the mechanism: a vector generated from the C
    # would follow the C's bugs and one generated from here would follow this
    # file's. tools/tests/test_mkkfw.sh makes the same argument about mkkfw.py's own
    # header. Changing the wire format means changing both literals, on
    # purpose, and one of these two cases failing is what makes "on purpose"
    # happen.
    golden = bytes((0x34, 0x12,              # seq = 0x1234
                    0x05, 0x00,              # x   = 5
                    0xFA, 0x00,              # y   = 250
                    0xCD, 0x00,              # w   = 205
                    0x3C, 0x00,              # h   = 60
                    0x45, 0x23, 0x01, 0x00))  # len = 74565
    check("the header is exactly 14 bytes on both sides",
          kmirror.HDR_LEN == len(golden), f"HDR_LEN={kmirror.HDR_LEN}")
    check("the golden header parses to the fields the firmware wrote",
          struct.unpack(kmirror.HDR_FMT, golden) == (0x1234, 5, 250, 205, 60,
                                                     74565),
          f"got {struct.unpack(kmirror.HDR_FMT, golden)}")
    check("and re-packs to the same bytes",
          struct.pack(kmirror.HDR_FMT, 0x1234, 5, 250, 205, 60, 74565)
          == golden)

    # And both decoders really do peel that header off a stream, rather than
    # only struct.unpack agreeing with itself: this is the path a frame
    # actually takes.
    body = bytes(range(74565 % 251)) * 1
    gold_frame = struct.pack(kmirror.HDR_FMT, 0x1234, 5, 250, 205, 60,
                             len(body)) + body
    d = kmirror.BleFrameDecoder()
    out = d.feed(gold_frame)
    check("the BLE decoder reads the golden header off a real frame",
          len(out) == 1 and out[0][:5] == (0x1234, 5, 250, 205, 60),
          f"got {out[0][:5] if out else out}")
    d = kmirror.MirrorDecoder()
    out = d.feed(base64.b64encode(gold_frame).decode())
    check("the cable decoder reads the same header through base64",
          len(out) == 1 and out[0][:5] == (0x1234, 5, 250, 205, 60),
          f"got {out[0][:5] if out else out}")

    # A frame in, the same frame out, on each wire's own encoding.
    jpeg = bytes(range(256)) * 12
    blob = struct.pack(kmirror.HDR_FMT, 9, 0, 40, 205, 60, len(jpeg)) + jpeg

    b64 = base64.b64encode(blob).decode()
    d = kmirror.MirrorDecoder()
    out = []
    for i in range(0, len(b64), 216):
        out += d.feed(b64[i:i + 216])
    check("cable decoder reassembles a frame byte for byte",
          len(out) == 1 and out[0][5] == jpeg and out[0][0] == 9)

    d = kmirror.BleFrameDecoder()
    out = []
    for i in range(0, len(blob), 244):
        out += d.feed(blob[i:i + 244])
    check("BLE decoder reassembles the same frame byte for byte",
          len(out) == 1 and out[0][5] == jpeg and out[0][0] == 9)

    # A header that cannot be a real frame - zero bytes, or more than the
    # firmware could ever produce - is garbage, and the decoder must step
    # over it rather than stall forever on a length nothing will satisfy.
    # This is a guard against noise, NOT the abort protocol: the abort is a
    # console line on both transports, because a marker framed by length
    # cannot be told from the body bytes it follows.
    for label, bogus in (("zero-length", 0), ("impossibly long", 1 << 20)):
        d = kmirror.BleFrameDecoder()
        out = d.feed(struct.pack(kmirror.HDR_FMT, 3, 0, 0, 0, 0, bogus) + blob)
        check(f"BLE decoder steps over a {label} header",
              len(out) == 1 and out[0][0] == 9 and out[0][5] == jpeg)

    # The two control records are handled in ONE place both readers call, and
    # both must clear the decoder: this is the drift that killed a live BLE
    # session once already, when the wake-up path called abort() on a decoder
    # that had never had one.
    class FakeBoard:
        def __init__(self):
            self.sent = []

        def send_line(self, line):
            self.sent.append(line)

    for cls in (kmirror.MirrorDecoder, kmirror.BleFrameDecoder):
        for record, restarts in ((">|mabort", 0), (">|mpause", 1)):
            d = cls()
            d.feed("QUJD" if cls is kmirror.MirrorDecoder else b"\x01\x02\x03")
            b = FakeBoard()
            handled = kmirror.control_line(record, d, b, threading.Lock(),
                                           2, 70, False)
            starts = [s for s in b.sent if s.startswith("mirror_start")]
            check(f"{cls.__name__}: {record} is handled and clears the decoder",
                  handled and d._buf == b"")
            check(f"{cls.__name__}: {record} restarts the stream {restarts}x",
                  len(starts) == restarts, f"sent={b.sent}")

    d = kmirror.MirrorDecoder()
    check("a data record is not mistaken for a control record",
          not kmirror.control_line(">|m=QUJD", d, None, None, 2, 70, False))

    print()
    if FAILURES:
        print(f"FAILED: {', '.join(FAILURES)}")
        return 1
    print("all good")
    return 0


if __name__ == "__main__":
    sys.exit(main())
