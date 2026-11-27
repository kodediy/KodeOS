#!/usr/bin/env bash
# Exercises tools/release/flash.sh against a fake esptool: what each option
# writes, where, and what it leaves behind, without a board.
#   usage: tools/tests/test_flash_sh.sh
#   exit:  0 all cases passed, 1 something regressed
#
# flash.sh runs under /bin/bash here on purpose: the factory runs it on Macs,
# whose /bin/bash is 3.2, where `set -u` and empty arrays bite.
set -uo pipefail
cd "$(dirname "$0")/../.."

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fail=0
ok()  { printf 'ok    %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fail=1; }

# --- fixture: a pack of one-line images and a fake python3 with esptool ---
pack="$work/pack"
mkdir -p "$pack"
cp tools/release/flash.sh tools/release/write_unchecked.py "$pack/"
for f in bootloader.bin partition-table.bin ota_data_initial.bin kodeos.bin apps.bin storage.bin; do
    echo "fake $f" > "$pack/$f"
done

# The fake answers esptool's version, then writes a line `call` and every
# argument it is given, one per line, to $FAKE_LOG; after an argument that
# names a file it adds `file <size> ff|data`, read while flash.sh still holds
# the file. It exits 2 on the call whose number is $FAKE_FAIL_CALL.
mkdir -p "$work/bin"
cat > "$work/bin/python3" <<'FAKE'
#!/bin/bash
if [ "$1" = "-m" ] && [ "$2" = "esptool" ] && [ "$3" = "version" ]; then
    echo "esptool v5.4.0"
    echo "5.4.0"
    exit 0
fi
printf 'call\n' >> "$FAKE_LOG"
n="$(grep -cx call "$FAKE_LOG")"
for a in "$@"; do
    printf '%s\n' "$a" >> "$FAKE_LOG"
    if [ -f "$a" ]; then
        size="$(wc -c < "$a" | tr -d ' ')"
        if [ "$(LC_ALL=C tr -d '\377' < "$a" | wc -c | tr -d ' ')" = 0 ]; then
            kind=ff
        else
            kind=data
        fi
        printf 'file %s %s\n' "$size" "$kind" >> "$FAKE_LOG"
    fi
done
[ "$n" = "${FAKE_FAIL_CALL:-0}" ] && exit 2
exit "${FAKE_STATUS:-0}"
FAKE
chmod +x "$work/bin/python3"

# run <name> [args...] - flash.sh on a fake port; argv in $work/<name>.log,
# its own output in $work/<name>.out
run() {
    local name="$1"; shift
    PATH="$work/bin:$PATH" FAKE_LOG="$work/$name.log" \
        /bin/bash "$pack/flash.sh" /dev/fake "$@" > "$work/$name.out" 2>&1
}
argv() {   # argv <name> [<n>] - the arguments of call <n> (all calls when left
           # out), on one line, space-padded; a path is cut to its last part,
           # so two runs' temp files compare equal
    printf ' %s' "$(awk -v n="${2:-0}" '/^call$/ { c++; next } /^file / { next } n == 0 || c == n { print }' "$work/$1.log" | sed '/^\/dev\//!s#^/.*/##' | tr '\n' ' ')"
}
calls() {  # calls <name> - how many times flash.sh ran python
    grep -cx call "$work/$1.log" 2>/dev/null || true
}
has_n() {  # has_n <name> <n> <words> - the words appear, in order, in call <n>
    case "$(argv "$1" "$2")" in *" $3 "*) return 0 ;; *) return 1 ;; esac
}
has() {    # has <name> <words> - the words appear in esptool's argv, in order
    case "$(argv "$1")" in *" $2 "*) return 0 ;; *) return 1 ;; esac
}
blank() {  # blank <name> <offset> - "<size> ff" of the file written at <offset>
    awk -v off="$2" '/^file / && p2 == off { print $2, $3; exit } { p2 = p1; p1 = $0 }' "$work/$1.log"
}
blank_dirs() {   # blank_dirs <name> - the folders the blank files esptool got were in
    grep '^/.*\.bin$' "$work/$1.log" | while read -r p; do dirname "$p"; done | sort -u
}
cleaned() {      # cleaned <name> - esptool got blank files, and their folders are gone
    local dirs d
    dirs="$(blank_dirs "$1")"
    [ -n "$dirs" ] || return 1
    for d in $dirs; do [ ! -e "$d" ] || return 1; done
}
ran() {    # ran <name> - esptool was asked to write, so the checks below see a real argv
    [ -f "$work/$1.log" ] && grep -qx 'write-flash' "$work/$1.log"
}
sawfile() {  # sawfile <name> - the fake saw write_unchecked.py as an existing file
    grep -q '^file ' <<<"$(grep -A1 -x 'write_unchecked.py' "$work/$1.log")"
}

# --- case 1: the default is the recovery flash, unchanged ---
run default
if has default "--erase-all" && has default "0xC40000 apps.bin" && has default "0x1800000 storage.bin" \
        && has default "0x20000 kodeos.bin"; then
    ok "default keeps --erase-all and writes the six images"
else
    bad "default does not keep --erase-all and the six images: $(argv default)"
fi

# --- case 2: --line, the production line's short write ---
run line --line
if ! ran line; then
    bad "--line did not write: $(cat "$work/line.out")"
elif has line "--erase-all"; then
    bad "--line still erases the whole chip"
else
    ok "--line has no --erase-all"
fi
if [ "$(calls line)" = 2 ]; then
    ok "--line makes two calls"
else
    bad "--line does not make two calls: $(calls line)"
fi
if has_n line 1 "-m esptool --chip esp32p4 -p /dev/fake --after no-reset write-flash 0x2000 bootloader.bin 0x8000 partition-table.bin 0x1B000 ota_data_initial.bin 0xB000 nvs.bin 0xC20000 coredump.bin 0xC40000 apps.bin 0x1A00000 app_pending.bin 0x1FFF000 app_pending_intent.bin" \
        && ! grep -q 'kodeos.bin' <<<"$(argv line 1)"; then
    ok "--line's first call writes the three small images and the blanks, with no kodeos.bin"
else
    bad "--line's first call is wrong: $(argv line 1)"
fi
if [ "$(argv line 2)" = " write_unchecked.py /dev/fake 0x20000 kodeos.bin " ] && sawfile line; then
    ok "--line's second call is write_unchecked.py on kodeos.bin at 0x20000, found beside flash.sh"
else
    bad "--line's second call is wrong: $(argv line 2)"
fi
if ! ran line || grep -qx 'apps.bin' "$work/line.log" || grep -qx 'storage.bin' "$work/line.log"; then
    bad "--line writes apps.bin or storage.bin"
else
    ok "--line writes no apps.bin and no storage.bin"
fi
for zone in "0xB000 65536" "0xC20000 131072" "0xC40000 12288" "0x1A00000 4096" "0x1FFF000 4096"; do
    set -- $zone
    got="$(blank line "$1")"
    if [ "$got" = "$2 ff" ]; then
        ok "--line blanks $2 bytes at $1"
    else
        bad "--line does not blank $2 bytes at $1 (got: ${got:-nothing})"
    fi
done
if ran line && [ "$(grep -c '^file .* ff$' "$work/line.log")" -eq 5 ]; then
    ok "--line blanks five zones and no more"
else
    bad "--line does not blank exactly five zones"
fi

# --- case 3: --line and --no-reset, in either order, and twice ---
run line_noreset --line --no-reset
run noreset_line --no-reset --line
if has_n line_noreset 1 "-m esptool --chip esp32p4 -p /dev/fake --before no-reset --after no-reset write-flash 0x2000 bootloader.bin 0x8000 partition-table.bin 0x1B000 ota_data_initial.bin 0xB000 nvs.bin 0xC20000 coredump.bin 0xC40000 apps.bin 0x1A00000 app_pending.bin 0x1FFF000 app_pending_intent.bin" \
        && [ "$(argv line_noreset 2)" = " write_unchecked.py /dev/fake 0x20000 kodeos.bin --no-reset " ] \
        && [ "$(argv noreset_line 1)" = "$(argv line_noreset 1)" ] \
        && [ "$(argv noreset_line 2)" = "$(argv line_noreset 2)" ]; then
    ok "--line --no-reset and --no-reset --line both write short with no reset, in both calls"
else
    bad "the two orders of --line and --no-reset differ: $(argv line_noreset) / $(argv noreset_line)"
fi
run twice --line --line
if ran twice && [ "$(argv twice)" = "$(argv line)" ]; then
    ok "--line given twice is --line"
else
    bad "--line given twice is not --line: $(argv twice)"
fi

# --- case 4: an unknown option writes nothing ---
if run unknown --fast; then
    bad "an unknown option should exit non-zero"
elif [ -e "$work/unknown.log" ]; then
    bad "an unknown option still ran esptool"
else
    ok "an unknown option exits non-zero and writes nothing"
fi
if grep -q 'usage' "$work/unknown.out"; then
    ok "an unknown option prints the usage"
else
    bad "an unknown option does not print the usage"
fi

# --- case 5: the blank files never outlive flash.sh ---
if ran line && cleaned line; then
    ok "a good write leaves no temp folder"
else
    bad "a good write left its blank files: $(blank_dirs line)"
fi
if FAKE_FAIL_CALL=1 run first_fails --line; then
    bad "a failing first call should make flash.sh fail"
elif [ "$(calls first_fails)" = 1 ] && cleaned first_fails; then
    ok "a failing first call stops flash.sh before write_unchecked.py and leaves no temp folder"
else
    bad "a failing first call ran $(calls first_fails) calls or left its blanks: $(blank_dirs first_fails)"
fi
if FAKE_FAIL_CALL=2 run second_fails --line; then
    bad "a failing second call should make flash.sh fail"
elif [ "$(calls second_fails)" = 2 ] && cleaned second_fails; then
    ok "a failing second call makes flash.sh fail and leaves no temp folder"
else
    bad "a failing second call ran $(calls second_fails) calls or left its blanks: $(blank_dirs second_fails)"
fi
if FAKE_STATUS=2 run failed --line; then
    bad "a failed write should make flash.sh fail"
elif ! ran failed; then
    bad "the failed case never reached esptool: $(cat "$work/failed.out")"
elif cleaned failed; then
    ok "a failed write leaves no temp folder"
else
    bad "a failed write left its blank files: $(blank_dirs failed)"
fi

if [ "$fail" -eq 0 ]; then
    echo "all cases passed"
else
    echo "one or more cases failed"
fi
exit "$fail"
