#!/usr/bin/env bash
# Factory-flash a Kode Dot from this pack: erase the whole chip, then write
# bootloader + partition table + blank otadata + kodeOS itself, at the same
# four offsets `idf.py flash` itself uses, the apps region with the welcome
# app installed (apps.bin: its directory and its image, no boot request) and
# the internal FAT with Kody's pack (kode/). "Recovery flash: bootloader +
# factory" in the kodeOS README is the long form of what this does.
#
#   usage: ./flash.sh /dev/tty.usbmodemXXXX [--no-reset]
#
# --no-reset is for a station that has already parked the P4 in its ROM and
# must not see it reset while the jig programs the C5 (the two share C5_EN):
# esptool resets the P4 neither before the write nor after it, and the P4 is
# left in download mode for the station to reset when the C5 is done.
#
# Needs python3 with esptool 5 or newer on it (`python3 -m pip install
# 'esptool>=5'`): esptool 5 renamed its commands to the dashed form this
# script uses (`write-flash`), and an older one refuses it. The script
# checks the version before it touches the board.
#
# This release erases the whole chip first (`write-flash --erase-all`, one
# esptool call). It crosses a table change that moves nvs over the old
# otadata and phy_init sectors and puts the apps region's base on bytes an
# old image left behind, so a targeted write is not enough - and every board
# leaves it on this table, so no stale v00.00.17 table copy at 0xA000 can
# survive it either. Once every board has been through this release, the
# erase comes back out.
#
# That means everything on the chip goes - the storage FAT is written anew
# with Kody's pack alone, and beyond the six things this script writes go
# settings, remembered networks,
# BLE bonds, the touch calibration, the apps installed before (only the
# welcome app is installed after), the mailbox, all of it.
# Copy anything you want to keep off the board before flashing (`file_get
# name=...` on the console); this script does not ask again.
set -euo pipefail
cd "$(dirname "$0")"

PORT="${1:?usage: ./flash.sh /dev/tty.usbmodemXXXX [--no-reset]}"
RESET=()
case "${2:-}" in
    "") ;;
    --no-reset) RESET=(--before no-reset --after no-reset) ;;
    *)
        echo "usage: ./flash.sh /dev/tty.usbmodemXXXX [--no-reset]" >&2
        exit 1 ;;
esac

# A clean macOS ships python3 and no `python`; an ESP-IDF shell has both.
if command -v python3 >/dev/null 2>&1; then
    PY=python3
elif command -v python >/dev/null 2>&1; then
    PY=python
else
    echo "error: no python3 on PATH; install Python 3 and 'esptool>=5'" >&2
    exit 1
fi

# `esptool version` prints its bare version on its last line, in 4.x and 5.x.
ESPTOOL_VER="$("$PY" -m esptool version 2>/dev/null | tail -n 1 || true)"
case "$ESPTOOL_VER" in
    [5-9].*|[1-9][0-9].*) ;;
    "")
        echo "error: esptool is not installed for $PY; run: $PY -m pip install 'esptool>=5'" >&2
        exit 1 ;;
    *)
        echo "error: esptool $ESPTOOL_VER is too old, this pack needs 5 or newer; run: $PY -m pip install 'esptool>=5'" >&2
        exit 1 ;;
esac

# macOS's bash 3.2 calls an empty array unbound under `set -u`: hence the +.
"$PY" -m esptool --chip esp32p4 -p "$PORT" ${RESET[@]+"${RESET[@]}"} write-flash --erase-all \
  0x2000   bootloader.bin \
  0x8000   partition-table.bin \
  0x1B000  ota_data_initial.bin \
  0x20000  kodeos.bin \
  0xC40000 apps.bin \
  0x1800000 storage.bin

echo
if [ ${#RESET[@]} -gt 0 ]; then
    echo "Written. The P4 is still in its ROM: reset it to boot kodeOS."
fi
echo "Done. Verify over the console: 'boot_info' answers one record,"
echo "  >|running=factory addr=0x020000 size_kb=... otadata=blank reason=..."
echo "running=factory and otadata=blank are the two words that matter."
