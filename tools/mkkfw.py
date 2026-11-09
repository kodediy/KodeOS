#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wrap an ESP-IDF image in a Kode firmware container.

The header is 72 bytes, little-endian, and everything in it describes the
*decompressed* image rather than the payload that carries it. That is
deliberate on both counts:

  - the region allocator on the board needs the decompressed length before
    it writes the first byte, and Content-Length over HTTP describes the
    compressed payload instead;
  - the SHA-256 is of the image, because the image is the thing that has to
    be right. A digest of the payload would be satisfied by a correct
    transfer of a corrupt file.

Layout:

    0x00  4   magic "KFW1"
    0x04  2   header version (1)
    0x06  2   header length (72)
    0x08  1   target: 0 os, 1 app, 2 c5
    0x09  1   compression: 0 none, 1 deflate
    0x0A  2   flags: bit 0 = an app-data block follows this header
              (target app only), every other bit reserved, zero
    0x0C  4   payload length, the bytes that follow this header
    0x10  4   image length, after decompression
    0x14  32  SHA-256 of the decompressed image
    0x34  4   CRC32 of bytes 0x00..0x33
    0x38  16  name, NUL-padded; empty unless target is app

With flags bit 0 (`--appdata DIR`, target app only) the app's metadata
travels right after the header, before the payload, uncompressed. It seals
itself, so the image SHA-256 and the header CRC cover what they always did,
and `payload length` still counts the payload alone:

    0x00  4   magic "KAD1"
    0x04  2   json_len, 1..1024
    0x06  2   icon_len, 0 or 12288 (one 64x64 RGB565 + A8 icon.k64)
    0x08  4   CRC32 of the json_len + icon_len bytes that follow (the
              header's flavour, see esp_crc32)
    0x0C  json_len bytes of app.json, exactly as the board will store it
    ...   icon_len bytes of icon.k64

The sizes and offsets are KFW_AD_* in sdk/components/kdrv/apps/include/
kdrv_kfw.h; the board checks the block with the parser the folder's reader
uses, and validate() in kappdata.py is that parser's host twin.
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import zlib

import kappdata

MAGIC = b"KFW1"
HDR_VERSION = 1
HDR_LEN = 72
NAME_MAX = 16
ESP_IMAGE_MAGIC = 0xE9
TARGETS = {"os": 0, "app": 1, "c5": 2}
FLAG_APPDATA = 0x0001       # flags bit 0: an app-data block follows the header
AD_MAGIC = b"KAD1"          # the block's own magic
AD_HDR_LEN = 12             # magic, json_len, icon_len, crc
AD_JSON_MAX = 1024          # KFW_AD_JSON_MAX: the longest app.json a block carries
AD_ICON_LEN = kappdata.K64_BYTES   # KFW_AD_ICON_LEN: the one icon size, 12288
AD_ICON_NAME = "icon.k64"   # what the icon key reads once an icon is packed


def esp_crc32(data: bytes) -> int:
    """The CRC32 the board computes, which is not zlib's default.

    kdrv_kfw.c seals the header with esp_rom_crc32_le(UINT32_MAX, ...). The
    ROM routine inverts the seed it is given, so passing ~0 starts the raw
    CRC at 0 - where zlib's default starts it at ~0. Passing 0xFFFFFFFF as
    zlib's running value is what reproduces it. Getting this wrong produces
    a tool and a test that agree with each other and with nothing on the
    board, which is exactly how it was found.
    """
    return zlib.crc32(data, 0xFFFFFFFF) & 0xFFFFFFFF


def appdata_block(json_bytes: bytes, icon: bytes) -> bytes:
    """Return the app-data block for an app.json and an icon (maybe empty).

    The sizes are checked against what the board accepts, so a block that
    leaves here is one the decoder keeps rather than drops.
    """
    if not 0 < len(json_bytes) <= AD_JSON_MAX:
        raise ValueError(f"app.json: {len(json_bytes)} bytes once written, "
                         f"must be 1..{AD_JSON_MAX}")
    if len(icon) not in (0, AD_ICON_LEN):
        raise ValueError(f"icon: {len(icon)} bytes, must be 0 or {AD_ICON_LEN}")
    body = json_bytes + icon
    head = struct.pack("<4sHHI", AD_MAGIC, len(json_bytes), len(icon),
                       esp_crc32(body))
    if len(head) != AD_HDR_LEN:
        raise AssertionError(f"block header is {len(head)} bytes, "
                             f"must be {AD_HDR_LEN}")
    return head + body


def load_appdata(folder: str) -> tuple:
    """Read `folder` the way `kcli appdata put` does: (app.json bytes, icon).

    Raises ValueError naming the field that the board would refuse. The icon
    comes back empty when the folder has none.
    """
    path = os.path.join(folder, "app.json")
    try:
        with open(path, "rb") as fh:
            obj = kappdata.validate(json.loads(fh.read()))
    except OSError as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path}: {exc}") from exc

    icon = b""
    icon_path = kappdata.find_icon(folder, obj)
    if icon_path:
        try:
            icon = kappdata.to_k64(icon_path)
        except ImportError as exc:
            raise ValueError(f"icon: {icon_path} needs Pillow "
                             "(python3 -m pip install pillow)") from exc
        except OSError as exc:      # Pillow's "cannot identify image file"
            raise ValueError(f"icon: {icon_path}: {exc}") from exc
        obj["icon"] = AD_ICON_NAME   # the board reads what is packed
    elif "icon" in obj:
        raise ValueError(f"icon: {obj['icon']} is not in {folder}")
    body = json.dumps(obj, separators=(",", ":")).encode("ascii")
    if len(body) > AD_JSON_MAX:
        raise ValueError(f"app.json: {len(body)} bytes once written, "
                         f"longer than {AD_JSON_MAX}")
    return body, icon


def build(image: bytes, target: int, name: str, compress: bool,
          appdata: tuple = None) -> bytes:
    """Return the container for `image`. Pure, so the test can trust it.

    `appdata` is (json_bytes, icon_bytes_or_empty) and needs target app.
    """
    if appdata is not None and target != TARGETS["app"]:
        raise ValueError("only an app container carries app data")
    flags = 0
    block = b""
    if appdata is not None:
        flags = FLAG_APPDATA
        block = appdata_block(*appdata)
    payload = zlib.compress(image, 9) if compress else image
    head = struct.pack(
        "<4sHHBBHII32s",
        MAGIC,
        HDR_VERSION,
        HDR_LEN,
        target,
        1 if compress else 0,
        flags,
        len(payload),
        len(image),
        hashlib.sha256(image).digest(),
    )
    head += struct.pack("<I", esp_crc32(head))
    head += name.encode().ljust(NAME_MAX, b"\x00")
    if len(head) != HDR_LEN:
        raise AssertionError(f"header is {len(head)} bytes, must be {HDR_LEN}")
    return head + block + payload


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", required=True, choices=sorted(TARGETS),
                    help="where the board is meant to write this")
    ap.add_argument("--name", default="",
                    help="app name, for --target app only")
    ap.add_argument("--no-compress", action="store_true",
                    help="store the image as-is instead of deflating it")
    ap.add_argument("--appdata", metavar="DIR",
                    help="pack DIR's app.json and icon into the container "
                         "(--target app only)")
    ap.add_argument("input", help="the .bin to wrap")
    ap.add_argument("output", help="the .kfw to write")
    a = ap.parse_args()

    if len(a.name) >= NAME_MAX:
        print(f"a name must be under {NAME_MAX} bytes, "
              f"'{a.name}' is {len(a.name)}", file=sys.stderr)
        return 1
    if a.name and a.target != "app":
        print("only an app image carries a name", file=sys.stderr)
        return 1

    if a.appdata is not None and a.target != "app":
        print("only an app container carries --appdata", file=sys.stderr)
        return 1

    appdata = None
    if a.appdata is not None:
        try:
            appdata = load_appdata(a.appdata)
        except ValueError as exc:
            print(exc, file=sys.stderr)
            return 1

    try:
        with open(a.input, "rb") as fh:
            image = fh.read()
    except OSError as exc:
        print(f"cannot read {a.input}: {exc}", file=sys.stderr)
        return 1

    if not image:
        print(f"{a.input} is empty", file=sys.stderr)
        return 1
    if image[0] != ESP_IMAGE_MAGIC:
        print(f"{a.input} starts with 0x{image[0]:02x}, not the ESP image "
              f"magic 0x{ESP_IMAGE_MAGIC:02x} - this is not a firmware image",
              file=sys.stderr)
        return 1

    try:
        blob = build(image, TARGETS[a.target], a.name, not a.no_compress,
                     appdata)
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 1
    try:
        with open(a.output, "wb") as fh:
            fh.write(blob)
    except OSError as exc:
        print(f"cannot write {a.output}: {exc}", file=sys.stderr)
        return 1

    block_len = 0
    extra = ""
    if appdata is not None:
        block_len = AD_HDR_LEN + len(appdata[0]) + len(appdata[1])
        extra = (f", app data {len(appdata[0])} bytes of app.json + "
                 f"{len(appdata[1])} bytes of icon")
    ratio = (len(blob) - HDR_LEN - block_len) * 100 // len(image)
    print(f"{a.output}: {len(image)} bytes of image -> {len(blob)} bytes "
          f"({ratio}% of the original, target={a.target}{extra})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
