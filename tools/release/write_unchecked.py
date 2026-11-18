#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write one image to the P4's flash without esptool's read-back.

esptool reads every write back and compares an MD5, which costs 7.6 s for
kodeos.bin at the stub's 500 KB/s. The bootloader checks that image's SHA-256
at every boot anyway, so the read-back repeats a check the board makes. Only
flash.sh --line uses this script; nothing here says the image was verified.

    python3 write_unchecked.py PORT OFFSET FILE [--no-reset]
"""

import argparse
import sys
import time
import zlib

from esptool.cmds import detect_chip, run_stub, attach_flash, detect_flash_size, reset_chip
from esptool.util import flash_size_bytes, FatalError
from esptool.loader import timeout_per_mb, ERASE_WRITE_TIMEOUT_PER_MB

COMPRESSION_LEVEL = 9  # zlib's best, as esptool compresses
STUB_DEFLATE_BUFFER_SIZE = 32 * 1024  # the stub erases one block per buffer it inflates
FLASH_ERASE_BLOCK_SIZE = 64 * 1024  # what one of those erases clears


def block_timeout(inflated):
    """Seconds to wait after a block that inflated to `inflated` bytes.

    The stub erases ahead of the write: one block per buffer of the data just
    inflated, plus one more. esptool 5.4 adds those erases to the size it
    asks the timeout for; before any block there is nothing to wait for.
    """
    size = inflated
    if inflated > 0:
        erases = -(-inflated // STUB_DEFLATE_BUFFER_SIZE) + 1
        size += erases * FLASH_ERASE_BLOCK_SIZE
    return timeout_per_mb(ERASE_WRITE_TIMEOUT_PER_MB, size)


def write_image(port, offset, data, reset):
    """Write data at offset through the stub; return the seconds it took."""
    packed = zlib.compress(data, COMPRESSION_LEVEL)
    started = time.monotonic()

    esp = detect_chip(port, connect_mode="no-reset")
    esp = run_stub(esp)
    attach_flash(esp)
    # The stub's ID read on this chip carries 0xff in its top byte, which
    # esptool does not recognise, so no size comes back. Its own command line
    # then leaves the size alone too.
    flash_size = detect_flash_size(esp)
    if flash_size is not None:
        esp.flash_set_parameters(flash_size_bytes(flash_size))
    esp.flash_defl_begin(len(data), len(packed), offset)

    # Each block waits as long as the erase and write of what the previous
    # block inflated to, the erases the stub runs ahead included, as
    # esptool's write_flash does on the stub; the first one gets its floor.
    inflater = zlib.decompressobj()
    inflated = 0
    block_size = esp.FLASH_WRITE_SIZE
    for seq, start in enumerate(range(0, len(packed), block_size)):
        block = packed[start:start + block_size]
        esp.flash_defl_block(block, seq, timeout=block_timeout(inflated))
        inflated = len(inflater.decompress(block))

    esp.flash_defl_finish(reboot=False, timeout=block_timeout(inflated))
    if reset:
        reset_chip(esp, "hard-reset")
    return time.monotonic() - started


def main(argv):
    """Run the command line; return 0 when written, 1 on any error."""
    parser = argparse.ArgumentParser(description="Write one image, unchecked.")
    parser.add_argument("port")
    parser.add_argument("offset", type=lambda text: int(text, 0))
    parser.add_argument("file")
    parser.add_argument("--no-reset", action="store_true")
    args = parser.parse_args(argv)

    try:
        with open(args.file, "rb") as image:
            data = image.read()
        print("Writing %s at 0x%08x, unchecked..." % (args.file, args.offset))
        seconds = write_image(args.port, args.offset, data, not args.no_reset)
    except (FatalError, OSError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1

    print("Wrote %d bytes at 0x%08x in %.1f s; not read back: "
          "the bootloader checks its SHA-256 at boot" % (len(data), args.offset, seconds))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
