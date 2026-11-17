#!/usr/bin/env bash
# Checks the zones flash.sh --line blanks (its LINE_BLANKS) against the
# partition table and the apps region's format. Those zones are table facts
# written down a second time, so a table change that moves one has to stop
# the release here, not blank the wrong place on the production line.
#   usage: tools/check_line_blanks.sh <flash.sh> [<partitions.csv>] [<kapps_fmt.h>]
#   exit:  0 the five zones are the table's, 1 they are not (each one named)
set -euo pipefail

SECTOR=0x1000   # the flash's erase sector: the apps region's records, the mailbox's first sector and its intent are one each

root="$(cd "$(dirname "$0")/.." && pwd)"
flash="${1:?usage: tools/check_line_blanks.sh <flash.sh> [<partitions.csv>] [<kapps_fmt.h>]}"
table="${2:-$root/sdk/components/kbsp/partitions.csv}"
fmt="${3:-$root/sdk/components/kdrv/apps/include/kapps_fmt.h}"

hex() { printf '0x%X' "$(( $1 ))"; }

part() {      # part <name> - "<offset> <size>" of a partition in the table
    awk -F, -v n="$1" '
        { name = $1; gsub(/[[:space:]]/, "", name) }
        name == n { o = $4; s = $5; gsub(/[[:space:]]/, "", o); gsub(/[[:space:]]/, "", s); print o, s; exit }
    ' "$table"
}

fmt_val() {   # fmt_val <NAME> - a #define's value from kapps_fmt.h, without its U
    sed -n "s/^#define $1[[:space:]]*\(0x[0-9A-Fa-f]*\)U.*/\1/p" "$fmt"
}

need() {      # need <what> <value> - stop when a fact could not be read
    if [ -z "$2" ]; then
        echo "error: $1 not found" >&2
        exit 1
    fi
}

zones="$(awk "/^LINE_BLANKS='\$/ { on = 1; next } on && /^'\$/ { exit } on && NF { print }" "$flash")"
if [ -z "$zones" ]; then
    echo "error: flash.sh has no LINE_BLANKS" >&2
    exit 1
fi

nvs="$(part nvs)";                need "the nvs partition" "$nvs"
coredump="$(part coredump)";      need "the coredump partition" "$coredump"
apps="$(part apps)";              need "the apps partition" "$apps"
pending="$(part app_pending)";    need "the app_pending partition" "$pending"
req_off="$(fmt_val KAPPS_REQ_OFF)";                need "KAPPS_REQ_OFF" "$req_off"
intent_off="$(fmt_val KAPPS_PENDING_INTENT_OFF)";  need "KAPPS_PENDING_INTENT_OFF" "$intent_off"

# What the line must blank: name, offset, size, as the table and the format say.
set -- $nvs;      want_nvs="nvs $(hex "$1") $(hex "$2")"
set -- $coredump; want_coredump="coredump $(hex "$1") $(hex "$2")"
set -- $apps;     want_apps="apps $(hex "$1") $(hex "$req_off + $SECTOR")"
set -- $pending;  want_pending="app_pending $(hex "$1") $(hex "$SECTOR")"
                  want_intent="app_pending_intent $(hex "$1 + $intent_off") $(hex "$SECTOR")"

fail=0
for want in "$want_nvs" "$want_coredump" "$want_apps" "$want_pending" "$want_intent"; do
    set -- $want
    name="$1"; off="$2"; size="$3"
    got="$(printf '%s\n' "$zones" | awk -v n="$name" '$1 == n { print $2, $3; exit }')"
    if [ -z "$got" ]; then
        echo "error: flash.sh does not blank $name, the table wants $off size $size" >&2
        fail=1
        continue
    fi
    set -- $got
    got_off="$(hex "$1")"; got_size="$(hex "$2")"
    if [ "$got_off" != "$off" ] || [ "$got_size" != "$size" ]; then
        echo "error: flash.sh blanks $name at $got_off size $got_size, the table wants $off size $size" >&2
        fail=1
    fi
done

while read -r name off size; do
    case "$name" in
        nvs|coredump|apps|app_pending|app_pending_intent) ;;
        *)
            echo "error: flash.sh blanks $name at $(hex "$off") size $(hex "$size"), which is not one of the line's five zones" >&2
            fail=1 ;;
    esac
done <<< "$zones"

exit "$fail"
