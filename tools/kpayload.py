#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What both console clients need to speak the console's grammar and make
sense of an outbound payload.

A string goes to the board in quotes and comes back in quotes
(https://docs.kode.diy/internals/console-protocol): `quote()` writes one and `fields()`
reads a record back with its quoted values whole. `screenshot` and `file_get`
send bulk data as base64 inside ordinary `>|d=` records rather than as a raw
stream ("Getting bulk data back"), and the encoding is the same whichever pipe
carried it. So all of it lives here rather than twice: kcli.py drives the
cable and kble.py drives the air, and neither has an opinion about quoting,
base64 or BMP.

Nothing outside the standard library, deliberately. kble.py re-executes itself
inside a venv built for bleak, where pyserial does not exist - so importing
kcli.py to reach these functions would not work even once.
"""

import base64
import hashlib
import json
import os
import re
import zlib


class PayloadError(Exception):
    """The board said no, or said nothing, or sent something unreadable."""


# The three escapes the console's grammar has, and nothing else: any other
# backslash pair is kept exactly as typed, on both sides of the wire.
_ESCAPES = {'"': '"', "\\": "\\", "n": "\n"}


def quote(value):
    """A string as the console takes it: in quotes, with `"`, `\\` and a
    newline escaped.

    Every string key needs it - `file_get name="notes.txt"` - and nothing else
    may have it: a number, `on`/`off` or an enum member goes bare, and the
    board refuses the other form with `>err code=3`. See
    https://docs.kode.diy/internals/console-protocol.
    """
    out = value.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + out.replace("\n", "\\n") + '"'


def fields(record):
    """The `key=value` pairs of a `>|` record, as a dict.

    A quoted value is read back whole - spaces, `=` and escapes included -
    and handed over without its quotes, so `name="my notes.txt"` gives
    `my notes.txt`. A bare value is kept as it is; a bare word with no `=`
    (`>|pong`) is not a pair and is left out.
    """
    out = {}
    i, n = 0, len(record or "")
    while i < n:
        while i < n and record[i] in " \t":
            i += 1
        start = i
        while i < n and record[i] not in " \t=":
            i += 1
        key = record[start:i]
        if i < n and record[i] == "=" and i + 1 < n and record[i + 1] == '"':
            i += 2
            val = []
            while i < n and record[i] != '"':
                if record[i] == "\\" and i + 1 < n:
                    pair = record[i + 1]
                    val.append(_ESCAPES.get(pair, "\\" + pair))
                    i += 2
                else:
                    val.append(record[i])
                    i += 1
            i += 1
            out[key] = "".join(val)
        elif i < n and record[i] == "=":
            i += 1
            vstart = i
            while i < n and record[i] not in " \t":
                i += 1
            out[key] = record[vstart:i]
        else:
            while i < n and record[i] not in " \t":
                i += 1
    return out


# Where IDF stamps the project name in a bare image: esp_app_desc_t sits at
# 0x20 and opens with its magic word; the name is 32 bytes, 0x30 into it.
_APP_DESC_OFF = 0x20
_APP_DESC_MAGIC = 0xABCD5432
_APP_NAME_OFF = _APP_DESC_OFF + 0x30
_APP_NAME_LEN = 32


def project_name(image):
    """The project name a bare ESP image carries, or "" when it carries none.

    `install src="terminal" dst=app name=` sends it so the board knows which
    installed app the image replaces: when the region is full, that app's old
    copy makes room first, before any other app is evicted.
    """
    if len(image) < _APP_NAME_OFF + _APP_NAME_LEN or image[0] != 0xE9:
        return ""
    magic = int.from_bytes(image[_APP_DESC_OFF:_APP_DESC_OFF + 4], "little")
    if magic != _APP_DESC_MAGIC:
        return ""
    raw = image[_APP_NAME_OFF:_APP_NAME_OFF + _APP_NAME_LEN]
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


def head_fields(head):
    """The `key=value` pairs of a header record, as a dict.

    The header is the first record of a payload - `>|shot w= h= fmt= len=` or
    `>|file name= size= medium=` - and it is what a client checks its own
    arithmetic against.
    """
    return fields(head)


def decode_records(parts, expect=None):
    """Join `>|d=` record bodies and decode them.

    One decode over the concatenation, which is what the protocol guarantees:
    every record but the last is a whole number of base64 groups, so no `=`
    ever lands mid-stream.

    @param expect  What the header record promised, if it said. A different
                   number means the transfer was cut short - worth saying so
                   rather than writing a file that looks fine and is not.
    """
    data = base64.b64decode("".join(parts)) if parts else b""
    if (expect is not None) and (len(data) != expect):
        raise PayloadError(
            f"got {len(data)} bytes, the board promised {expect}")
    return data


def decode_whole_groups(pending, chunk):
    """Append `chunk` to `pending` and decode only whole 4-character groups.

    `decode_records` above gets to wait for a terminator before it decodes
    anything, because a bulk transfer has one. A continuous stream - the
    screen mirror's `>|m=` records, one frame's bytes running straight into
    the next's header with no terminator between them - does not, so it has
    to decode as records arrive without ever assuming a short tail is a
    complete group. Padding a partial group with `=` does not recover its
    prefix, it manufactures different bytes: a header read that way is
    garbage, not a short header.

    @param pending  Leftover characters from the last call, still short of a
                     group of four.
    @param chunk    The next record's base64 text.
    @return (bytes decoded this call, the new leftover - 0-3 characters)
    """
    pending += chunk
    whole = len(pending) - (len(pending) % 4)
    if whole == 0:
        return b"", pending
    return base64.b64decode(pending[:whole]), pending[whole:]


def png_from_bmp16(bmp):
    """Convert the board's 16bpp BMP to a PNG, with nothing installed.

    PNG is a handful of length-tag-payload-CRC chunks around a zlib stream,
    and zlib is in the standard library - so this costs thirty lines and saves
    both clients from needing Pillow to produce the format anybody actually
    wants to look at.
    """
    def u32(off):
        return int.from_bytes(bmp[off:off + 4], "little")

    if bmp[:2] != b"BM":
        raise PayloadError("that is not a BMP")
    offbits, w, h = u32(10), u32(18), u32(22)
    bpp = int.from_bytes(bmp[28:30], "little")
    if bpp != 16:
        raise PayloadError(f"expected 16bpp, got {bpp}")

    stride = w * 2
    rows = []
    for y in range(h):
        # BMP rows are bottom-up; PNG wants them top-down.
        start = offbits + ((h - 1 - y) * stride)
        row = bytearray(b"\x00")          # filter type 0, once per scanline
        for x in range(0, stride, 2):
            v = bmp[start + x] | (bmp[start + x + 1] << 8)
            r5, g6, b5 = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            # Replicate the high bits into the low ones so full-scale stays
            # full-scale: 0x1F must become 0xFF, not 0xF8.
            row += bytes(((r5 << 3) | (r5 >> 2),
                          (g6 << 2) | (g6 >> 4),
                          (b5 << 3) | (b5 >> 2)))
        rows.append(bytes(row))

    def chunk(tag, payload):
        return (len(payload).to_bytes(4, "big") + tag + payload
                + (zlib.crc32(tag + payload) & 0xFFFFFFFF).to_bytes(4, "big"))

    ihdr = (w.to_bytes(4, "big") + h.to_bytes(4, "big")
            + bytes((8, 2, 0, 0, 0)))     # 8-bit, truecolour, no interlace
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
            + chunk(b"IEND", b""))


def write_capture(path, bmp):
    """Save a capture as the BMP it is, or as a PNG when the name asks.

    @return (bytes written, a line worth printing).
    """
    data = png_from_bmp16(bmp) if path.lower().endswith(".png") else bmp
    with open(path, "wb") as f:
        f.write(data)
    w = int.from_bytes(bmp[18:22], "little")
    h = int.from_bytes(bmp[22:26], "little")
    return len(data), f"{path}: {w}x{h}, {len(data)} bytes"


# ---- Kody's pack ----------------------------------------------------------
#
# `assets-install DIR` sends a pack folder to /storage/kode.new/ and commits
# it (assets_begin, one file_put per file, assets_commit). This is what both
# clients send: the board judges everything else (kui_pack.h), so the host
# checks only what it must to send the right files - their names, that they
# are there, and their digests.

def assets_refusal(records):
    """The `why=` of the `>|assets ... result=refused` record the `assets_*`
    functions answer a refusal with, or None when no record refused.

    The functions answer in records and close with `>ok` or `>err`
    (kode_functions.h): the reason is in the record, not in a log line.
    """
    for rec in records:
        if rec.startswith("assets "):
            f = fields(rec)
            if f.get("result") == "refused":
                return f.get("why", "?")
    return None


PACK_FILE = re.compile(r"^[A-Za-z0-9_.-]{1,40}$")
PACK_ROLES = ("empty", "writing", "goodbye", "loading")
PACK_FILE_MAX = 64 * 1024


def assets_plan(folder):
    """[(name, bytes)] to send: kode.json first, with every digest, then each file once.

    Exits with a message for a name the board would refuse as a file of a
    pack, a file that is missing or over 64 KB, or a digest the manifest
    gives that the file does not match. Digests the manifest lacks are added.
    """
    with open(os.path.join(folder, "kode.json"), "rb") as f:
        manifest = json.loads(f.read())
    names = []

    def name(n):
        if not isinstance(n, str) or not PACK_FILE.match(n) or n.startswith(".") \
                or not (n.endswith(".json") or n.endswith(".json.z")):
            raise SystemExit(f"assets: {n!r} cannot be a file of a pack "
                             "(1-40 of A-Z a-z 0-9 _ . -, ending .json or .json.z)")
        if n not in names:
            names.append(n)

    for scene in manifest.get("scenes", []):
        name(scene.get("wake"))
        name(scene.get("loop"))
        for files in scene.get("on", {}).values():
            for n in files:
                name(n)
    for role in PACK_ROLES:
        if role in manifest:
            name(manifest[role])

    digests = dict(manifest.get("sha256", {}))
    files = []
    for n in names:
        path = os.path.join(folder, n)
        if not os.path.isfile(path):
            raise SystemExit(f"assets: {n} is not in {folder}")
        with open(path, "rb") as f:
            data = f.read()
        if len(data) > PACK_FILE_MAX:
            raise SystemExit(f"assets: {n} is {len(data)} bytes, over 64 KB")
        digest = hashlib.sha256(data).hexdigest()
        if n in digests and digests[n] != digest:
            raise SystemExit(f"assets: {n} does not match the digest kode.json gives it")
        digests[n] = digest
        files.append((n, data))
    manifest["sha256"] = digests
    body = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    return [("kode.json", body)] + files
