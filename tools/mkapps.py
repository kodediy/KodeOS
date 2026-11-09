#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the `apps` region's first bytes with one app installed in it.

The factory pack writes this at the region's base, so a board leaves the
programming station with the welcome app (CONFIG_KODEOS_WELCOME_APP) already
installed, exactly as `install dst=app` would have left it:

    +0x0000   directory copy A: one live entry, gen 1, sealed with its CRC
    +0x1000   directory copy B: erased (0xFF), so A is the one a boot picks
    +0x2000   boot request: erased - no app runs until kodeOS asks for one
    +0x10000  the app's image, as built (KAPPS_DATA_OFF, the first MMU page)

No boot request is written on purpose. The factory boots kodeOS on this image
for its tests, and the factory reset writes the welcome app's request itself,
last, right before ship mode.

Every number is read from kapps_fmt.h and partitions.csv, the files the board
is built from, so this tool cannot drift from the format it writes:

    tools/mkapps.py --project welcome welcome.bin apps.bin
"""
import argparse
import re
import struct
import sys

import mkkfw

FMT_H = "sdk/components/kdrv/apps/include/kapps_fmt.h"
PARTITIONS_CSV = "sdk/components/kbsp/partitions.csv"
APPS_PART = "apps"
FMT_NAMES = ("KAPPS_DIR_MAGIC", "KAPPS_FMT_VERSION", "KAPPS_DIR_A_OFF",
             "KAPPS_DIR_B_OFF", "KAPPS_REQ_OFF", "KAPPS_DATA_OFF",
             "KAPPS_ALIGN", "KAPPS_MAX_ENTRIES", "KAPPS_ENTRY_VALID")
ERASED = b"\xff"
DIR_GEN = 1                 # a fresh region's generation, as kdrv_apps formats it
ENTRY_SEQ = 1               # the first install's seq: next_seq() of an empty directory
DIR_HEAD = "<IIII"          # magic, version, gen, count
DIR_ENTRY = "<IIII"         # offset, size, seq, flags
WRITE_ALIGN = 4             # esptool writes whole words

# esp_app_desc_t sits at the start of the image's first segment: after the
# 24-byte image header and the 8-byte segment header. Its project name is the
# 32 bytes after magic, secure_version, two reserved words and the version.
APP_DESC_OFF = 0x20
APP_DESC_MAGIC = 0xABCD5432
APP_DESC_PROJECT_OFF = APP_DESC_OFF + 0x30
APP_DESC_PROJECT_LEN = 32


def read_fmt(path: str) -> dict:
    """The KAPPS_* numbers this tool needs, as kapps_fmt.h defines them."""
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    out = {}
    for name in FMT_NAMES:
        m = re.search(rf"^#define\s+{name}\s+(\(1U << 0\)|0x[0-9A-Fa-f]+U|[0-9]+U)",
                      text, re.MULTILINE)
        if m is None:
            raise ValueError(f"{name} is not defined in {path}")
        word = m.group(1)
        if word == "(1U << 0)":
            out[name] = 1
        else:
            out[name] = int(word.rstrip("U"), 0)
    return out


def read_region(path: str) -> tuple:
    """(offset, size) of the `apps` partition in partitions.csv."""
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            cols = [c.strip() for c in line.split("#", 1)[0].split(",")]
            if cols and cols[0] == APPS_PART:
                return int(cols[3], 0), int(cols[4], 0)
    raise ValueError(f"no '{APPS_PART}' partition in {path}")


def project_of(image: bytes) -> str:
    """The project name an ESP-IDF image carries, as AO_AppMgr reads it."""
    if len(image) < APP_DESC_PROJECT_OFF + APP_DESC_PROJECT_LEN:
        raise ValueError("the image is too short to carry an app descriptor")
    if image[0] != mkkfw.ESP_IMAGE_MAGIC:
        raise ValueError(f"the image starts with 0x{image[0]:02x}, not the ESP image "
                         f"magic 0x{mkkfw.ESP_IMAGE_MAGIC:02x}")
    (magic,) = struct.unpack_from("<I", image, APP_DESC_OFF)
    if magic != APP_DESC_MAGIC:
        raise ValueError("the image carries no app descriptor")
    raw = image[APP_DESC_PROJECT_OFF:APP_DESC_PROJECT_OFF + APP_DESC_PROJECT_LEN]
    return raw.split(b"\x00", 1)[0].decode("ascii", "replace")


def build(image: bytes, fmt: dict, region_size: int) -> bytes:
    """Return the region's bytes from its base to the end of `image`. Pure."""
    data_off = fmt["KAPPS_DATA_OFF"]
    if data_off % fmt["KAPPS_ALIGN"] != 0:
        raise AssertionError("KAPPS_DATA_OFF is not on an MMU page")
    if not 0 < len(image) <= region_size - data_off:
        raise ValueError(f"the image is {len(image)} bytes; the region holds "
                         f"{region_size - data_off} after its directory")

    entries = [(data_off, len(image), ENTRY_SEQ, fmt["KAPPS_ENTRY_VALID"])]
    entries += [(0, 0, 0, 0)] * (fmt["KAPPS_MAX_ENTRIES"] - len(entries))
    body = struct.pack(DIR_HEAD, fmt["KAPPS_DIR_MAGIC"], fmt["KAPPS_FMT_VERSION"],
                       DIR_GEN, 1)
    body += b"".join(struct.pack(DIR_ENTRY, *e) for e in entries)
    directory = body + struct.pack("<I", mkkfw.esp_crc32(body))

    out = bytearray(ERASED * data_off)
    a = fmt["KAPPS_DIR_A_OFF"]
    if a + len(directory) > fmt["KAPPS_DIR_B_OFF"]:
        raise AssertionError("directory copy A overlaps copy B")
    out[a:a + len(directory)] = directory
    out += image
    out += ERASED * (-len(out) % WRITE_ALIGN)
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--project", required=True,
                    help="the project name the image must carry")
    ap.add_argument("--fmt", default=FMT_H, help=f"kapps_fmt.h (default {FMT_H})")
    ap.add_argument("--partitions", default=PARTITIONS_CSV,
                    help=f"the partition table (default {PARTITIONS_CSV})")
    ap.add_argument("input", help="the app's .bin")
    ap.add_argument("output", help="the region image to write")
    a = ap.parse_args()

    try:
        fmt = read_fmt(a.fmt)
        offset, size = read_region(a.partitions)
        with open(a.input, "rb") as fh:
            image = fh.read()
        project = project_of(image)
        if project != a.project:
            raise ValueError(f"{a.input} is the project '{project}', not '{a.project}'")
        blob = build(image, fmt, size)
        with open(a.output, "wb") as fh:
            fh.write(blob)
    except (OSError, ValueError) as exc:
        print(f"mkapps: {exc}", file=sys.stderr)
        return 1

    print(f"{a.output}: '{project}' installed, {len(image)} bytes of image, "
          f"write at 0x{offset:X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
