#!/usr/bin/env bash
#
# A kodeos.bin has two ceilings: its slot (factory, ota_0, ota_1, 4 MB),
# which IDF's own build already checks, and the mailbox's image space
# (KAPPS_PENDING_INTENT_OFF, app_pending less its intent sector), which an
# update through the mailbox needs. Today the slot is the smaller, but the
# check takes whichever is, read out of partitions.csv and kapps_fmt.h so
# neither number can drift from what the board uses. It runs on every push
# (see tests.yml) as well as at release, with assemble_release.sh calling
# this rather than carrying a second copy of the parsing.
#
#   usage: tools/check_os_size.sh <kodeos.bin>
#   stdout: one line, always -
#             "<name> <n> of <ceiling> bytes, <left> left (<pct>%)"
#           plus a GitHub Actions "::warning::" line once under 10% of the
#           ceiling is left
#   exit:   0 under the ceiling, 1 past it or if a ceiling cannot be parsed
set -euo pipefail
cd "$(dirname "$0")/.."

bin_path="${1:?usage: tools/check_os_size.sh <kodeos.bin>}"

if [ ! -f "$bin_path" ]; then
    echo "error: missing required input: $bin_path" >&2
    exit 1
fi

kapps_fmt_h="sdk/components/kdrv/apps/include/kapps_fmt.h"
if [ ! -f "$kapps_fmt_h" ]; then
    echo "error: missing required input: $kapps_fmt_h" >&2
    exit 1
fi

define_line="$(grep -E '^#define[[:space:]]+KAPPS_PENDING_INTENT_OFF[[:space:]]+0x[0-9A-Fa-f]+U' "$kapps_fmt_h" || true)"
if [ -z "$define_line" ]; then
    echo "error: could not find KAPPS_PENDING_INTENT_OFF in $kapps_fmt_h" >&2
    exit 1
fi
mailbox_ceiling_hex="$(printf '%s\n' "$define_line" | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^0x[0-9A-Fa-f]+U$/) { print $i; exit } }')"
mailbox_ceiling_hex="${mailbox_ceiling_hex#0x}"
mailbox_ceiling_hex="${mailbox_ceiling_hex%U}"
if [ -z "$mailbox_ceiling_hex" ]; then
    echo "error: could not parse KAPPS_PENDING_INTENT_OFF's value out of $kapps_fmt_h" >&2
    exit 1
fi
mailbox_ceiling=$((16#${mailbox_ceiling_hex}))

partitions_csv="sdk/components/kbsp/partitions.csv"
if [ ! -f "$partitions_csv" ]; then
    echo "error: missing required input: $partitions_csv" >&2
    exit 1
fi
slot_hex="$(awk -F, '$1 ~ /^factory[[:space:]]*$/ { gsub(/[[:space:]]/, "", $5); print $5; exit }' "$partitions_csv")"
slot_hex="${slot_hex#0x}"
if [ -z "$slot_hex" ]; then
    echo "error: could not parse the factory slot's size out of $partitions_csv" >&2
    exit 1
fi
slot_size=$((16#${slot_hex}))

if [ "$slot_size" -lt "$mailbox_ceiling" ]; then
    ceiling="$slot_size"
    ceiling_name="the OS slot, 0x${slot_hex} in ${partitions_csv}"
else
    ceiling="$mailbox_ceiling"
    ceiling_name="the mailbox, KAPPS_PENDING_INTENT_OFF = 0x${mailbox_ceiling_hex} in ${kapps_fmt_h}"
fi

bin_name="$(basename "$bin_path")"
bin_size="$(wc -c < "$bin_path" | tr -d '[:space:]')"
left=$((ceiling - bin_size))
pct_left=$((left * 100 / ceiling))

printf '%s %d of %d bytes, %d left (%d%%)\n' "$bin_name" "$bin_size" "$ceiling" "$left" "$pct_left"

if [ "$pct_left" -lt 10 ]; then
    printf '::warning::%s is within 10%% of its ceiling (%d bytes, %d%% left of %d)\n' \
        "$bin_name" "$left" "$pct_left" "$ceiling"
fi

if [ "$bin_size" -gt "$ceiling" ]; then
    printf 'error: %s is %d bytes, past its ceiling of %d bytes (%s)\n' \
        "$bin_name" "$bin_size" "$ceiling" "$ceiling_name" >&2
    exit 1
fi
