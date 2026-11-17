#!/usr/bin/env bash
#
# Turns a finished `idf.py build` into the four release artifacts kodeOS
# ships on GitHub: the factory pack (the files this release's
# erase-everything flash writes, per https://docs.kode.diy/internals/boot-model,
# the apps region with the welcome app installed among them, plus
# a flash.sh, a README.txt covering the same ground and the
# THIRD_PARTY_NOTICES.txt for what kodeos.bin contains), kodeos.bin
# standalone, for `os update` or the mailbox, and the welcome app's image and
# its app data, to install it again on a board that lost it. Used by
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
#         the FAT does not carry exactly kode/ and the welcome app's data
#         folder, or the welcome app's image is not
#         the project the build names, or the image or that data folder is not
#         the one tools/build_welcome.sh stamped from apps_image/welcome.pin
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
require "${templates_dir}/write_unchecked.py"
require "${templates_dir}/FACTORY_README.txt"
require "${templates_dir}/THIRD_PARTY_NOTICES.txt"

# A kodeos.bin must fit its 4 MB slot and the mailbox's image space, which an
# update through the mailbox needs. tools/check_os_size.sh carries the one
# copy of that check - reading both ceilings out of partitions.csv and
# kapps_fmt.h - and runs on every push in tests.yml as well as here, so the
# two guards cannot drift apart.
tools/check_os_size.sh "${build_dir}/kodeos.bin" || exit 1

# The FAT ships Kody's pack and the welcome app's data folder (checked further
# down, with the app), and nothing else: a bench file left in storage_image/
# must not reach a customer. A C5 image is one of them: the factory closes
# the C5 with a signed image of the C5's own release, and a closed C5 takes
# no unsigned one.
stage_fs="${build_dir}/storage_stage"
extra="$(cd "$stage_fs" && ls -A | grep -vxE 'kode|\.kode' || true)"
if [ -n "$extra" ]; then
    echo "error: the FAT image holds more than kode/ and .kode/: $(echo $extra)" >&2
    echo "  move bench files out of storage_image/ and build again" >&2
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
# The image must be the one tools/build_welcome.sh built from the pin: one
# copied in by hand has no stamp, and one built from another commit or a
# dirty tree has the wrong one. The stamp is
# "<commit> <sha256 of the image> <digest of the data folder>".
read -r _ _ welcome_pin < "${apps_dir}/welcome.pin"
read -r welcome_commit welcome_sum welcome_data < "${apps_dir}/${welcome}.commit"
if [ -z "$welcome_pin" ] || [ "$welcome_commit" != "$welcome_pin" ]; then
    echo "error: ${welcome}.bin was built from ${welcome_commit:-nothing}, welcome.pin names ${welcome_pin:-nothing}" >&2
    echo "  run tools/build_welcome.sh, then build and assemble again" >&2
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
# Apps reads an app's icon, colour and description from its data folder, and
# an app's metadata travels apart from its image: an install writes no such
# folder. So the FAT image carries the welcome app's, and only that one under
# .kode/. tools/build_welcome.sh built it from the pinned app's appdata/ and
# put its digest in the stamp; a folder edited since, or left by another
# build, has another digest.
meta_root="${stage_fs}/.kode"
meta="${meta_root}/appsdata/${welcome}"
if [ ! -d "$meta" ]; then
    echo "error: the FAT image carries no data folder for the welcome app (.kode/appsdata/${welcome})" >&2
    echo "  run tools/build_welcome.sh before the build, then build and assemble again" >&2
    exit 1
fi
stray="$(cd "$meta_root" && find . -mindepth 1 ! -path './appsdata' ! -path "./appsdata/${welcome}" ! -path "./appsdata/${welcome}/*" | sort)"
if [ -n "$stray" ]; then
    echo "error: the FAT image's .kode/ holds more than the welcome app's data folder: $(echo $stray)" >&2
    echo "  empty storage_image/.kode/, run tools/build_welcome.sh and build again" >&2
    exit 1
fi
if ! meta_sum="$(python3 tools/kappdata.py digest "$meta")"; then
    echo "error: the FAT image's data folder for the welcome app is not one the board reads" >&2
    exit 1
fi
if [ -z "${welcome_data:-}" ] || [ "$meta_sum" != "$welcome_data" ]; then
    echo "error: the FAT image's data folder for the welcome app is not the one tools/build_welcome.sh stamped" >&2
    echo "  run tools/build_welcome.sh before the build, then build and assemble again" >&2
    exit 1
fi
apps_off="$(awk -F, '$1 ~ /^apps[[:space:]]*$/ { gsub(/[[:space:]]/, "", $4); print $4; exit }' sdk/components/kbsp/partitions.csv)"
flash_off="$(awk '$2 == "apps.bin" { print $1; exit }' "${templates_dir}/flash.sh")"
if [ -z "$apps_off" ] || [ -z "$flash_off" ] || [ $((apps_off)) -ne $((flash_off)) ]; then
    echo "error: flash.sh writes apps.bin at ${flash_off:-nowhere}, the apps region starts at ${apps_off:-nowhere}" >&2
    exit 1
fi
# The zones flash.sh --line blanks are table facts too; it names each wrong one.
tools/check_line_blanks.sh "${templates_dir}/flash.sh"

rm -rf "$stage"
mkdir -p "$stage" "$out_dir"

python3 tools/mkapps.py --project "$welcome" "${apps_dir}/${welcome}.bin" "$stage/apps.bin" >/dev/null

cp "${build_dir}/bootloader/bootloader.bin"            "$stage/"
cp "${build_dir}/partition_table/partition-table.bin"  "$stage/"
cp "${build_dir}/ota_data_initial.bin"                 "$stage/"
cp "${build_dir}/kodeos.bin"                           "$stage/"
cp "${build_dir}/storage.bin"                          "$stage/"
cp "${templates_dir}/flash.sh"                         "$stage/"
cp "${templates_dir}/write_unchecked.py"               "$stage/"
cp "${templates_dir}/FACTORY_README.txt"               "$stage/README.txt"
cp "${templates_dir}/THIRD_PARTY_NOTICES.txt"          "$stage/"
chmod +x "$stage/flash.sh" "$stage/write_unchecked.py"

( cd "$out_dir" && rm -f "${pack_name}.zip" && zip -rq "${pack_name}.zip" "${pack_name}" -x "${pack_name}/" )
rm -rf "$stage"

cp "${build_dir}/kodeos.bin" "${out_dir}/kodeos-v${version}.bin"

# The welcome app as a person puts it back on a board that lost it: its image,
# for `install`, and its data folder under the app's own name, which is what
# `kcli.py appdata put` takes once unzipped.
app_name="${welcome}-v${version}"
cp "${apps_dir}/${welcome}.bin" "${out_dir}/${app_name}.bin"
rm -rf "${out_dir:?}/${welcome}"
mkdir "${out_dir}/${welcome}"
cp "$meta"/* "${out_dir}/${welcome}/"
( cd "$out_dir" && rm -f "${app_name}-appdata.zip" && zip -rq "${app_name}-appdata.zip" "${welcome}" -x "${welcome}/" )
rm -rf "${out_dir:?}/${welcome}"

( cd "$out_dir" && sha256sum "${pack_name}.zip" "kodeos-v${version}.bin" "${app_name}.bin" "${app_name}-appdata.zip" > SHA256SUMS.txt )

echo "release artifacts ready in ${out_dir}/:"
ls -la "${out_dir}"
