#!/usr/bin/env bash
# Exercises tools/check_line_blanks.sh: the release's flash.sh passes, and a
# zone moved, shortened, missing or added - in flash.sh or in the table - is
# refused and named.
#   usage: tools/tests/test_check_line_blanks.sh
#   exit:  0 all cases passed, 1 something regressed
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
script="$PWD/tools/check_line_blanks.sh"
flash="$PWD/tools/release/flash.sh"
table="$PWD/sdk/components/kbsp/partitions.csv"

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

fail=0
check() {   # check <name> <expected_exit> <fragment> <flash.sh> [<partitions.csv>]
    local out rc
    out="$("$script" "$4" "${5:-$table}" 2>&1)"
    rc=$?
    if [ "$rc" -ne "$2" ]; then
        printf 'FAIL  %s: expected exit %s, got %s: %s\n' "$1" "$2" "$rc" "$out"
        fail=1
    elif [ -n "$3" ] && [[ "$out" != *"$3"* ]]; then
        printf 'FAIL  %s: the output does not say %q: %s\n' "$1" "$3" "$out"
        fail=1
    else
        printf 'ok    %s\n' "$1"
    fi
}
variant() { # variant <name> <sed program> - $scratch/<name>.sh, flash.sh with one change
    sed "$2" "$flash" > "$scratch/$1.sh"
    if cmp -s "$flash" "$scratch/$1.sh"; then
        printf 'FAIL  %s: the fixture changed nothing in flash.sh\n' "$1"
        fail=1
    fi
}

check "the release's flash.sh passes" 0 "" "$flash"

variant moved_nvs 's/^nvs                0xB000 /nvs                0xC000 /'
check "a moved nvs is refused and named" 1 \
    "error: flash.sh blanks nvs at 0xC000 size 0x10000, the table wants 0xB000 size 0x10000" \
    "$scratch/moved_nvs.sh"

variant short_apps 's/^apps               0xC40000  0x3000$/apps               0xC40000  0x2000/'
check "a short apps head is refused" 1 \
    "error: flash.sh blanks apps at 0xC40000 size 0x2000, the table wants 0xC40000 size 0x3000" \
    "$scratch/short_apps.sh"

variant moved_intent 's/^app_pending_intent 0x1FFF000 /app_pending_intent 0x1FFE000 /'
check "an intent off the mailbox's last sector is refused" 1 \
    "error: flash.sh blanks app_pending_intent at 0x1FFE000 size 0x1000, the table wants 0x1FFF000 size 0x1000" \
    "$scratch/moved_intent.sh"

variant no_mailbox_head '/^app_pending        0x1A00000 0x1000$/d'
check "a mailbox whose first sector is not blanked is refused" 1 \
    "error: flash.sh does not blank app_pending, the table wants 0x1A00000 size 0x1000" \
    "$scratch/no_mailbox_head.sh"

variant no_coredump '/^coredump           0xC20000  0x20000$/d'
check "a missing zone is refused" 1 \
    "error: flash.sh does not blank coredump, the table wants 0xC20000 size 0x20000" \
    "$scratch/no_coredump.sh"

variant extra_zone '/^app_pending_intent 0x1FFF000 0x1000$/a\
phy_init           0x1D000   0x1000
'
check "an extra zone is refused" 1 \
    "error: flash.sh blanks phy_init at 0x1D000 size 0x1000, which is not one of the line's five zones" \
    "$scratch/extra_zone.sh"

sed 's/^nvs,           data, nvs,      0xB000,/nvs,           data, nvs,      0xC000,/' "$table" > "$scratch/moved.csv"
if cmp -s "$table" "$scratch/moved.csv"; then
    printf 'FAIL  the table fixture changed nothing\n'
    fail=1
fi
check "a table that moves nvs refuses the old flash.sh" 1 \
    "error: flash.sh blanks nvs at 0xB000 size 0x10000, the table wants 0xC000 size 0x10000" \
    "$flash" "$scratch/moved.csv"

variant no_blanks "/^LINE_BLANKS='$/,/^'$/d"
check "a flash.sh with no zones at all is refused" 1 \
    "error: flash.sh has no LINE_BLANKS" \
    "$scratch/no_blanks.sh"

if [ "$fail" -eq 0 ]; then
    echo "all cases passed"
else
    echo "one or more cases failed"
fi
exit "$fail"
