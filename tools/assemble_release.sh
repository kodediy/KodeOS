#!/usr/bin/env bash
#
# Turns a finished `idf.py build` into the two release artifacts kodeOS
# ships on GitHub: the factory pack (the files this release's
# erase-everything flash writes, per https://docs.kode.diy/internals/boot-model,
# the apps region with the welcome app installed among them, plus
# a flash.sh, a README.txt covering the same ground and the
# THIRD_PARTY_NOTICES.txt for what kodeos.bin contains) and kodeos.bin
# standalone, for `os update` or the mailbox. Used by
# .github/workflows/release.yml; safe to run locally too, e.g. to check what
# a release would contain before tagging.
#
#   usage: tools/assemble_release.sh <version> [<build_dir>] [<out_dir>] [<apps_dir>]
#     version   e.g. 00.00.02 - no leading "v", see validate_release_tag.sh
#     build_dir default: build   (idf.py's output directory)
#     out_dir   default: dist
#     apps_dir  default: apps_image (holds <CONFIG_KODEOS_WELCOME_APP>.bin)
#   exit: 0 on success, 1 if a required input file is missing,
#         kodeos.bin is past its ceiling (tools/check_os_size.sh),
#         the FAT does not carry exactly kode/ and the C5 image the SDK pins,
#         or the welcome app's image is not the project the build names or
#         not the one tools/build_welcome.sh stamped from apps_image/welcome.pin
set -euo pipefail
cd "$(dirname "$0")/.."

version="${1:?usage: tools/assemble_release.sh <version> [<build_dir>] [<out_dir>] [<apps_dir>]}"
build_dir="${2:-build}"
out_dir="${3:-dist}"
apps_dir="${4:-apps_image}"
templates_dir="tools/release"

pack_name="kodeos-factory-v${version}"
stage="${out_dir}/${pack_name}"

require() {   # require <path> - fail with one clear message per missing input
    if [ ! -f "$1" ]; then
        echo "error: missing required input: $1" >&2
        exit 1
    fi
}

require "${build_dir}/bootloader/bootloader.bin"
require "${build_dir}/partition_table/partition-table.bin"
require "${build_dir}/ota_data_initial.bin"
require "${build_dir}/kodeos.bin"
require "${build_dir}/storage.bin"
require "${templates_dir}/flash.sh"
require "${templates_dir}/FACTORY_README.txt"
require "${templates_dir}/THIRD_PARTY_NOTICES.txt"

# A kodeos.bin must fit its 4 MB slot and the mailbox's image space, which an
# update through the mailbox needs. tools/check_os_size.sh carries the one
# copy of that check - reading both ceilings out of partitions.csv and
# kapps_fmt.h - and runs on every push in tests.yml as well as here, so the
# two guards cannot drift apart.
tools/check_os_size.sh "${build_dir}/kodeos.bin" || exit 1

# The FAT ships Kody's pack and the C5 image this release was built against,
# and nothing else: a bench file left in storage_image/ must not reach a
# customer, and a C5 image from another commit would not speak this kodeOS's
# KODE wire. The stamp is what tools/build_c5.sh wrote, "<commit> <sha256>".
stage_fs="${build_dir}/storage_stage"
require "${stage_fs}/c5_slave.bin"
require "${build_dir}/c5_slave.commit"
extra="$(cd "$stage_fs" && ls -A | grep -vxE 'kode|c5_slave\.bin' || true)"
if [ -n "$extra" ]; then
    echo "error: the FAT image holds more than kode/ and c5_slave.bin: $(echo $extra)" >&2
    echo "  move bench files out of storage_image/ and build again" >&2
    exit 1
fi
pinned="$(git -C sdk ls-tree HEAD c5 | awk '{ print $3 }')"
read -r stamp_commit stamp_sum < "${build_dir}/c5_slave.commit"
if [ -z "$pinned" ] || [ "$stamp_commit" != "$pinned" ]; then
    echo "error: c5_slave.bin was built from ${stamp_commit:-nothing}, the SDK pins ${pinned:-nothing}" >&2
    echo "  run tools/build_c5.sh with the SDK's C5 checked out, then build again" >&2
    exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then
    actual_sum="$(sha256sum "${stage_fs}/c5_slave.bin" | awk '{ print $1 }')"
else
    actual_sum="$(shasum -a 256 "${stage_fs}/c5_slave.bin" | awk '{ print $1 }')"
fi
if [ "$stamp_sum" != "$actual_sum" ]; then
    echo "error: the c5_slave.bin in the FAT is not the one tools/build_c5.sh stamped" >&2
    exit 1
fi

# A board leaves the factory with the welcome app installed: the factory
# reset that a PASS runs keeps it and writes its boot request before ship
# mode, so the customer's first power-on runs it. The image must be the
# project this kodeOS names, or the reset would not find it and the PASS
# would fail at its `find` step. flash.sh writes it at the region's base,
# and the region's base is the table's.
require "${build_dir}/config/sdkconfig.json"
welcome="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["KODEOS_WELCOME_APP"])' \
           "${build_dir}/config/sdkconfig.json")"
require "${apps_dir}/${welcome}.bin"
require "${apps_dir}/${welcome}.commit"
require "${apps_dir}/welcome.pin"
# The image must be the one tools/build_welcome.sh built from the pin, the
# same rule as the C5's: a .bin copied in by hand has no stamp, and one built
# from another commit or a dirty tree has the wrong one.
read -r _ _ welcome_pin < "${apps_dir}/welcome.pin"
read -r welcome_commit welcome_sum < "${apps_dir}/${welcome}.commit"
if [ -z "$welcome_pin" ] || [ "$welcome_commit" != "$welcome_pin" ]; then
    echo "error: ${welcome}.bin was built from ${welcome_commit:-nothing}, welcome.pin names ${welcome_pin:-nothing}" >&2
    echo "  run tools/build_welcome.sh, then assemble again" >&2
    exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then
    welcome_actual="$(sha256sum "${apps_dir}/${welcome}.bin" | awk '{ print $1 }')"
else
    welcome_actual="$(shasum -a 256 "${apps_dir}/${welcome}.bin" | awk '{ print $1 }')"
fi
if [ "$welcome_sum" != "$welcome_actual" ]; then
    echo "error: the ${welcome}.bin in ${apps_dir} is not the one tools/build_welcome.sh stamped" >&2
    exit 1
fi
apps_off="$(awk -F, '$1 ~ /^apps[[:space:]]*$/ { gsub(/[[:space:]]/, "", $4); print $4; exit }' sdk/components/kbsp/partitions.csv)"
flash_off="$(awk '$2 == "apps.bin" { print $1; exit }' "${templates_dir}/flash.sh")"
if [ -z "$apps_off" ] || [ -z "$flash_off" ] || [ $((apps_off)) -ne $((flash_off)) ]; then
    echo "error: flash.sh writes apps.bin at ${flash_off:-nowhere}, the apps region starts at ${apps_off:-nowhere}" >&2
    exit 1
fi

rm -rf "$stage"
mkdir -p "$stage" "$out_dir"

python3 tools/mkapps.py --project "$welcome" "${apps_dir}/${welcome}.bin" "$stage/apps.bin" >/dev/null

cp "${build_dir}/bootloader/bootloader.bin"            "$stage/"
cp "${build_dir}/partition_table/partition-table.bin"  "$stage/"
cp "${build_dir}/ota_data_initial.bin"                 "$stage/"
cp "${build_dir}/kodeos.bin"                           "$stage/"
cp "${build_dir}/storage.bin"                          "$stage/"
cp "${templates_dir}/flash.sh"                         "$stage/"
cp "${templates_dir}/FACTORY_README.txt"               "$stage/README.txt"
cp "${templates_dir}/THIRD_PARTY_NOTICES.txt"          "$stage/"
chmod +x "$stage/flash.sh"

( cd "$out_dir" && rm -f "${pack_name}.zip" && zip -rq "${pack_name}.zip" "${pack_name}" -x "${pack_name}/" )
rm -rf "$stage"

cp "${build_dir}/kodeos.bin" "${out_dir}/kodeos-v${version}.bin"

( cd "$out_dir" && sha256sum "${pack_name}.zip" "kodeos-v${version}.bin" > SHA256SUMS.txt )

echo "release artifacts ready in ${out_dir}/:"
ls -la "${out_dir}"
