#!/usr/bin/env bash
# Exercises tools/assemble_release.sh against a fabricated build/ directory,
# without needing a real idf.py build.
#   usage: tools/tests/test_assemble_release.sh
#   exit:  0 all cases passed, 1 something regressed
set -uo pipefail
cd "$(dirname "$0")/../.."

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fail=0
ok()  { printf 'ok    %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fail=1; }

# --- fixture: a fake `idf.py build` output ---
build_dir="$work/build"
mkdir -p "$build_dir/bootloader" "$build_dir/partition_table"
echo "fake bootloader"       > "$build_dir/bootloader/bootloader.bin"
echo "fake partition table"  > "$build_dir/partition_table/partition-table.bin"
echo "fake otadata"          > "$build_dir/ota_data_initial.bin"
echo "fake kodeos v00.00.99" > "$build_dir/kodeos.bin"
echo "fake storage with kode/" > "$build_dir/storage.bin"

# The FAT's staging: Kody's pack and the C5 image, stamped with the commit the
# SDK pins, as tools/build_c5.sh and the root CMake leave them.
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi | awk '{ print $1 }'; }
pinned_c5="$(git -C sdk ls-tree HEAD c5 | awk '{ print $3 }')"
stage_fs() {   # stage_fs <build_dir> - a FAT staging the release accepts
    mkdir -p "$1/storage_stage/kode"
    echo "fake manifest" > "$1/storage_stage/kode/kode.json"
    echo "fake c5 image" > "$1/storage_stage/c5_slave.bin"
    printf '%s %s\n' "$pinned_c5" "$(sha256 "$1/storage_stage/c5_slave.bin")" > "$1/c5_slave.commit"
}
stage_fs "$build_dir"

# The welcome app: the name the build gives it, and an image that carries it,
# just real enough for tools/mkapps.py (test_mkapps.py judges its output).
welcome_cfg() {   # welcome_cfg <build_dir> <name>
    mkdir -p "$1/config"
    printf '{ "KODEOS_WELCOME_APP": "%s" }\n' "$2" > "$1/config/sdkconfig.json"
}
fake_app() {      # fake_app <path> <project> - the image, its pin and its stamp
    local pin=1111111111111111111111111111111111111111
    python3 -c '
import struct, sys
img = bytearray(0x1000)
img[0] = 0xE9
struct.pack_into("<I", img, 0x20, 0xABCD5432)
name = sys.argv[2].encode()
img[0x50:0x50 + len(name)] = name
open(sys.argv[1], "wb").write(img)' "$1" "$2"
    printf 'git@example.invalid:app.git apps/x %s\n' "$pin" > "$(dirname "$1")/welcome.pin"
    printf '%s %s\n' "$pin" "$(sha256 "$1")" > "${1%.bin}.commit"
}
welcome_cfg "$build_dir" welcome
apps_dir="$work/apps_image"
mkdir -p "$apps_dir"
fake_app "$apps_dir/welcome.bin" welcome

out_dir="$work/dist"

# --- case 1: happy path ---
if tools/assemble_release.sh 00.00.99 "$build_dir" "$out_dir" "$apps_dir" >/dev/null; then
    ok "assemble_release.sh exits 0 on a complete build dir"
else
    bad "assemble_release.sh exited nonzero on a complete build dir"
fi

zip_file="$out_dir/kodeos-factory-v00.00.99.zip"
bin_file="$out_dir/kodeos-v00.00.99.bin"
sums_file="$out_dir/SHA256SUMS.txt"

[ -f "$zip_file" ]  && ok "factory zip exists"    || bad "factory zip missing: $zip_file"
[ -f "$bin_file" ]  && ok "standalone bin exists" || bad "standalone bin missing: $bin_file"
[ -f "$sums_file" ] && ok "SHA256SUMS.txt exists" || bad "SHA256SUMS.txt missing: $sums_file"

expected_entries="$(printf 'kodeos-factory-v00.00.99/bootloader.bin\nkodeos-factory-v00.00.99/partition-table.bin\nkodeos-factory-v00.00.99/ota_data_initial.bin\nkodeos-factory-v00.00.99/kodeos.bin\nkodeos-factory-v00.00.99/apps.bin\nkodeos-factory-v00.00.99/storage.bin\nkodeos-factory-v00.00.99/flash.sh\nkodeos-factory-v00.00.99/README.txt\nkodeos-factory-v00.00.99/THIRD_PARTY_NOTICES.txt' | sort)"
actual_entries="$(unzip -Z1 "$zip_file" 2>/dev/null | sort)"
if [ "$actual_entries" = "$expected_entries" ]; then
    ok "zip contains exactly the 9 expected files"
else
    bad "zip contents mismatch"
    diff <(echo "$expected_entries") <(echo "$actual_entries")
fi

extract_dir="$work/extracted"
mkdir -p "$extract_dir"
unzip -qo "$zip_file" -d "$extract_dir"
if [ -x "$extract_dir/kodeos-factory-v00.00.99/flash.sh" ]; then
    ok "flash.sh keeps its executable bit inside the zip"
else
    bad "flash.sh lost its executable bit"
fi

apps_bin="$extract_dir/kodeos-factory-v00.00.99/apps.bin"
if cmp -s "$apps_bin" "$apps_dir/welcome.bin" 65536 0; then
    ok "apps.bin carries the welcome app's image at the region's first data page"
else
    bad "apps.bin does not carry the welcome app's image at 0x10000"
fi

if diff -q "$build_dir/kodeos.bin" "$bin_file" >/dev/null; then
    ok "standalone kodeos-v00.00.99.bin matches build/kodeos.bin"
else
    bad "standalone bin content does not match build/kodeos.bin"
fi

if ( cd "$out_dir" && sha256sum -c SHA256SUMS.txt >/dev/null 2>&1 ); then
    ok "SHA256SUMS.txt verifies"
else
    bad "SHA256SUMS.txt does not verify"
fi

# --- case 2: missing input fails loudly instead of shipping a partial pack ---
incomplete_dir="$work/build-incomplete"
mkdir -p "$incomplete_dir/bootloader" "$incomplete_dir/partition_table"
echo "fake bootloader"      > "$incomplete_dir/bootloader/bootloader.bin"
echo "fake partition table" > "$incomplete_dir/partition_table/partition-table.bin"
echo "fake otadata"         > "$incomplete_dir/ota_data_initial.bin"
# kodeos.bin deliberately missing

if tools/assemble_release.sh 00.00.99 "$incomplete_dir" "$work/dist-incomplete" >/dev/null 2>&1; then
    bad "assemble_release.sh should have failed on a missing kodeos.bin"
else
    ok "assemble_release.sh rejects an incomplete build dir"
fi

# --- case 3: a kodeos.bin past its ceiling fails loudly and names it ---
# The ceiling is the smaller of the OS slot (factory, 4 MB, in
# sdk/components/kbsp/partitions.csv) and the mailbox's image space
# (KAPPS_PENDING_INTENT_OFF, sdk/components/kdrv/apps/include/kapps_fmt.h,
# 6 MB less its intent sector). Today that is the slot.
oversize_dir="$work/build-oversize"
mkdir -p "$oversize_dir/bootloader" "$oversize_dir/partition_table"
echo "fake bootloader"       > "$oversize_dir/bootloader/bootloader.bin"
echo "fake partition table"  > "$oversize_dir/partition_table/partition-table.bin"
echo "fake otadata"          > "$oversize_dir/ota_data_initial.bin"
echo "fake storage"          > "$oversize_dir/storage.bin"
os_ceiling=$((0x400000))
head -c "$((os_ceiling + 1))" /dev/zero > "$oversize_dir/kodeos.bin"

oversize_err="$work/oversize.err"
if tools/assemble_release.sh 00.00.99 "$oversize_dir" "$work/dist-oversize" >/dev/null 2>"$oversize_err"; then
    bad "assemble_release.sh should have refused a kodeos.bin past its ceiling"
else
    ok "assemble_release.sh rejects a kodeos.bin past its ceiling"
fi

if grep -qi '0x400000\|4194304' "$oversize_err"; then
    ok "the refusal names the ceiling"
else
    bad "the refusal does not name the ceiling"
    cat "$oversize_err" >&2
fi

# --- case 4: check_os_size.sh directly, comfortably under the ceiling ---
# assemble_release.sh calls this rather than carrying its own copy of the
# parsing (case 3 above already covers that call-through); these three cases
# exercise the script's own interface, run on every push in tests.yml.
under_bin="$work/under-kodeos.bin"
head -c "$((os_ceiling - 1000000))" /dev/zero > "$under_bin"

under_out="$work/under.out"
if tools/check_os_size.sh "$under_bin" >"$under_out" 2>&1; then
    ok "check_os_size.sh exits 0 comfortably under the ceiling"
else
    bad "check_os_size.sh should have exited 0 comfortably under the ceiling"
    cat "$under_out" >&2
fi

if grep -q "of $os_ceiling bytes" "$under_out"; then
    ok "check_os_size.sh prints the summary line"
else
    bad "check_os_size.sh did not print the summary line"
    cat "$under_out" >&2
fi

if grep -q '::warning::' "$under_out"; then
    bad "check_os_size.sh should not warn comfortably under the ceiling"
else
    ok "check_os_size.sh does not warn comfortably under the ceiling"
fi

# --- case 5: check_os_size.sh directly, within 10% of the ceiling ---
near_bin="$work/near-kodeos.bin"
head -c "$((os_ceiling - 100000))" /dev/zero > "$near_bin"

near_out="$work/near.out"
if tools/check_os_size.sh "$near_bin" >"$near_out" 2>&1; then
    ok "check_os_size.sh exits 0 within 10% of the ceiling"
else
    bad "check_os_size.sh should have exited 0 within 10% of the ceiling"
    cat "$near_out" >&2
fi

if grep -q '::warning::' "$near_out"; then
    ok "check_os_size.sh warns within 10% of the ceiling"
else
    bad "check_os_size.sh did not warn within 10% of the ceiling"
    cat "$near_out" >&2
fi

# --- case 6: check_os_size.sh directly, one byte over the ceiling ---
over_bin="$work/over-kodeos.bin"
head -c "$((os_ceiling + 1))" /dev/zero > "$over_bin"

over_out="$work/over.out"
if tools/check_os_size.sh "$over_bin" >"$over_out" 2>&1; then
    bad "check_os_size.sh should have exited 1 one byte over the ceiling"
else
    ok "check_os_size.sh exits 1 one byte over the ceiling"
fi

if grep -qi '0x400000\|4194304' "$over_out"; then
    ok "check_os_size.sh's message names the ceiling"
else
    bad "check_os_size.sh's message does not name the ceiling"
    cat "$over_out" >&2
fi

# --- case 7: the FAT's C5 image must be the one the SDK pins ---
c5_case() {   # c5_case <name> <mutation> - a complete build dir, then one change
    local dir="$work/build-$1"
    cp -R "$build_dir" "$dir"
    eval "$2"
    tools/assemble_release.sh 00.00.99 "$dir" "$work/dist-$1" "$apps_dir" >/dev/null 2>"$work/$1.err"
}
if c5_case other-commit 'printf "%s %s\n" 0000000000000000000000000000000000000000 "$(sha256 "$dir/storage_stage/c5_slave.bin")" > "$dir/c5_slave.commit"'; then
    bad "assemble_release.sh should refuse a C5 image built from another commit"
else
    ok "assemble_release.sh refuses a C5 image built from another commit"
fi
if c5_case dirty 'printf "%s-dirty %s\n" "$pinned_c5" "$(sha256 "$dir/storage_stage/c5_slave.bin")" > "$dir/c5_slave.commit"'; then
    bad "assemble_release.sh should refuse a C5 image built from a dirty tree"
else
    ok "assemble_release.sh refuses a C5 image built from a dirty tree"
fi
if c5_case swapped 'echo "another image" > "$dir/storage_stage/c5_slave.bin"'; then
    bad "assemble_release.sh should refuse a C5 image that is not the stamped one"
else
    ok "assemble_release.sh refuses a C5 image that is not the stamped one"
fi
if c5_case no-c5 'rm "$dir/storage_stage/c5_slave.bin"'; then
    bad "assemble_release.sh should refuse a FAT with no C5 image"
else
    ok "assemble_release.sh refuses a FAT with no C5 image"
fi
if c5_case bench-file 'echo "bench app" > "$dir/storage_stage/buttons_led.kfw"' ; then
    bad "assemble_release.sh should refuse a bench file in the FAT"
else
    ok "assemble_release.sh refuses a bench file in the FAT"
fi
if grep -q 'buttons_led.kfw' "$work/bench-file.err"; then
    ok "the refusal names the bench file"
else
    bad "the refusal does not name the bench file"
    cat "$work/bench-file.err" >&2
fi

# --- case 8: the welcome app must be there and be the project the build names ---
welcome_case() {   # welcome_case <name> <mutation> - a complete build and apps dir, then one change
    local dir="$work/build-$1" apps="$work/apps-$1"
    cp -R "$build_dir" "$dir"
    cp -R "$apps_dir" "$apps"
    eval "$2"
    tools/assemble_release.sh 00.00.99 "$dir" "$work/dist-$1" "$apps" >/dev/null 2>"$work/$1.err"
}
if welcome_case no-welcome 'rm "$apps/welcome.bin"'; then
    bad "assemble_release.sh should refuse a release with no welcome app"
else
    ok "assemble_release.sh refuses a release with no welcome app"
fi
if welcome_case other-project 'fake_app "$apps/welcome.bin" kodeos'; then
    bad "assemble_release.sh should refuse a welcome image of another project"
else
    ok "assemble_release.sh refuses a welcome image of another project"
fi
if welcome_case no-stamp 'rm "$apps/welcome.commit"'; then
    bad "assemble_release.sh should refuse a welcome image with no stamp"
else
    ok "assemble_release.sh refuses a welcome image with no stamp"
fi
if welcome_case other-commit 'printf "%s %s\n" 2222222222222222222222222222222222222222 "$(sha256 "$apps/welcome.bin")" > "$apps/welcome.commit"'; then
    bad "assemble_release.sh should refuse a welcome image built from another commit"
else
    ok "assemble_release.sh refuses a welcome image built from another commit"
fi
if welcome_case swapped 'printf "x" >> "$apps/welcome.bin"'; then
    bad "assemble_release.sh should refuse a welcome image that is not the stamped one"
else
    ok "assemble_release.sh refuses a welcome image that is not the stamped one"
fi
if welcome_case renamed 'welcome_cfg "$dir" hello; fake_app "$apps/hello.bin" hello'; then
    ok "assemble_release.sh takes the welcome app the build names"
else
    bad "assemble_release.sh should take the welcome app the build names"
    cat "$work/renamed.err" >&2
fi

if [ "$fail" -eq 0 ]; then
    echo "all cases passed"
else
    echo "one or more cases failed"
fi
exit "$fail"
