#!/usr/bin/env bash
# Factory-flash a Kode Dot from this pack: erase the whole chip, then write
# bootloader + partition table + blank otadata + kodeOS itself, at the same
# four offsets `idf.py flash` itself uses, the apps region with the welcome
# app installed (apps.bin: its directory and its image, no boot request) and
# the internal FAT with Kody's pack (kode/). "Recovery flash: bootloader +
# factory" in the kodeOS README is the long form of what this does.
#
#   usage: ./flash.sh /dev/tty.usbmodemXXXX [--no-reset] [--line]
#
# --no-reset is for a station that has already parked the P4 in its ROM and
# must not see it reset while the jig programs the C5 (the two share C5_EN):
# esptool resets the P4 neither before the write nor after it, and the P4 is
# left in download mode for the station to reset when the C5 is done.
#
# --line is the production line's write of a bare Main (E1), not a recovery.
# It erases nothing whole: it writes the four system images and puts 0xFF
# over the five places a previous pass can leave something that acts at
# boot (LINE_BLANKS below). The apps region and the FAT are not written:
# the assembled Kode fills them later from the station's SD (E5).
#
# Needs python3 with esptool 5 or newer on it (`python3 -m pip install
# 'esptool>=5'`): esptool 5 renamed its commands to the dashed form this
# script uses (`write-flash`), and an older one refuses it. The script
# checks the version before it touches the board.
#
# Without --line this is the recovery flash, and it erases the whole chip
# first (`write-flash --erase-all`, one esptool call), on purpose: a board
# may come with an older table (one that put nvs over today's otadata and
# phy_init sectors, or left a table copy at 0xA000) or with a person's data
# anywhere, and only a whole erase promises that none of it survives.
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

USAGE="usage: ./flash.sh /dev/tty.usbmodemXXXX [--no-reset] [--line]"
PORT="${1:?$USAGE}"
shift
RESET=()
LINE=0
for opt in "$@"; do
    case "$opt" in
        --no-reset) RESET=(--before no-reset --after no-reset) ;;
        --line) LINE=1 ;;
        *)
            echo "$USAGE" >&2
            exit 1 ;;
    esac
done

# What --line blanks, one zone per line: name, offset, size. Each is a place a
# previous pass can leave something that acts at boot:
#   nvs                 the station's lock and every stored setting;
#   coredump            a previous panic, which `info` would report;
#   apps                the region's directory A, directory B and boot
#                       request: a stale request makes the bootloader jump
#                       to an app;
#   app_pending         the mailbox's first sector: an image that starts
#                       there is adopted and launched at boot;
#   app_pending_intent  the mailbox's intent sector, which kodeOS erases
#                       with the first whenever it empties the mailbox.
# tools/check_line_blanks.sh checks every line against the partition table
# and the apps region's format before a release is assembled.
LINE_BLANKS='
nvs                0xB000    0x10000
coredump           0xC20000  0x20000
apps               0xC40000  0x3000
app_pending        0x1A00000 0x1000
app_pending_intent 0x1FFF000 0x1000
'

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
if [ "$LINE" -eq 1 ]; then
    # esptool writes what it is given, 0xFF included, so each zone is a file
    # of 0xFF bytes; they live in a temp folder that goes when the script does.
    blank_dir="$(mktemp -d)"
    trap 'rm -rf "$blank_dir"' EXIT
    BLANKS=()
    while read -r name off size; do
        [ -n "$name" ] || continue
        head -c "$((size))" /dev/zero | LC_ALL=C tr '\000' '\377' > "$blank_dir/$name.bin"
        BLANKS+=("$off" "$blank_dir/$name.bin")
    done <<< "$LINE_BLANKS"
    "$PY" -m esptool --chip esp32p4 -p "$PORT" ${RESET[@]+"${RESET[@]}"} write-flash \
      0x2000   bootloader.bin \
      0x8000   partition-table.bin \
      0x1B000  ota_data_initial.bin \
      0x20000  kodeos.bin \
      "${BLANKS[@]}"
else
    "$PY" -m esptool --chip esp32p4 -p "$PORT" ${RESET[@]+"${RESET[@]}"} write-flash --erase-all \
      0x2000   bootloader.bin \
      0x8000   partition-table.bin \
      0x1B000  ota_data_initial.bin \
      0x20000  kodeos.bin \
      0xC40000 apps.bin \
      0x1800000 storage.bin
fi

echo
if [ ${#RESET[@]} -gt 0 ]; then
    echo "Written. The P4 is still in its ROM: reset it to boot kodeOS."
fi
echo "Done. Verify over the console: 'boot_info' answers one record,"
echo "  >|running=factory addr=0x020000 size_kb=... otadata=blank reason=..."
echo "running=factory and otadata=blank are the two words that matter."
