#!/usr/bin/env bash
#
# Builds the coprocessor's firmware from the pinned source in sdk/c5/ and puts
# the image where the storage partition picks it up, with a stamp of the
# commit it was built from (storage_image/c5_slave.commit): every factory pack
# carries the C5 image of its own release, and assemble_release.sh refuses one
# whose stamp is not the commit the SDK pins.
#
# It exists because c5_slave.bin is the one thing in storage_image/ that this
# repository cannot rebuild on its own: kodeos.bin comes out of `idf.py
# build` here, that one needs a different target (esp32c5) and a
# different project. The submodule supplies the source at an exact commit;
# this turns it into the artifact, so "which firmware is on our C5" has an
# answer anyone can reproduce rather than a binary somebody once copied.
#
# Host and slave must run the same esp-hosted version - kdrv pins
# espressif/esp_hosted and the slave carries its own copy - so building from
# the pinned commit is what keeps that pairing true. Moving the pin is a
# deliberate, paired change; see https://docs.kode.diy/sdk/kdrv/radio.
#
#   usage: tools/build_c5.sh
#   exit:  0 on success, 1 if the submodule is missing or the build fails
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -f sdk/c5/CMakeLists.txt ]; then
    echo "sdk/c5/ is empty - the SDK submodule and its own are not checked out." >&2
    echo "  git submodule update --init --recursive" >&2
    exit 1
fi

if [ -z "${IDF_PATH:-}" ]; then
    echo "ESP-IDF is not on this shell. Source its export.sh first." >&2
    exit 1
fi

# Some ESP-IDF installations put idf.py on PATH; others (the EIM ones) define
# it as a shell function, which a child script cannot see. Fall back to the
# file itself, which is always there, run with whichever python the activated
# environment put in front.
if command -v idf.py >/dev/null 2>&1; then
    idf() { idf.py "$@"; }
else
    idf() { python "$IDF_PATH/tools/idf.py" "$@"; }
fi

# A separate build directory from the C5's own, so building here never
# fights a build someone is doing inside the submodule - and so the target
# switch (esp32c5, not esp32p4) cannot leak into anyone else's tree.
build_dir="build_c5"

echo "Building the C5 slave firmware (esp32c5) from sdk/c5/ ..."
idf -C sdk/c5 -B "$build_dir" set-target esp32c5
idf -C sdk/c5 -B "$build_dir" build

image="$build_dir/kode_c5.bin"
if [ ! -f "$image" ]; then
    echo "the build produced no $image" >&2
    exit 1
fi

mkdir -p storage_image
cp "$image" storage_image/c5_slave.bin

# The stamp: "<commit> <sha256>". A tree with local changes is no commit at
# all, so its stamp says -dirty and no release accepts it.
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi | awk '{ print $1 }'; }
commit="$(git -C sdk/c5 rev-parse HEAD)"
if [ -n "$(git -C sdk/c5 status --porcelain)" ]; then
    commit="${commit}-dirty"
fi
printf '%s %s\n' "$commit" "$(sha256 storage_image/c5_slave.bin)" > storage_image/c5_slave.commit

pinned="$(git -C sdk ls-tree HEAD c5 | awk '{ print $3 }')"
if [ "$commit" != "$pinned" ]; then
    echo "warning: built sdk/c5 @ $commit, but the SDK pins $pinned - a release will refuse it" >&2
fi

pin="$(git -C sdk/c5 rev-parse --short HEAD)"
size="$(wc -c < storage_image/c5_slave.bin | tr -d ' ')"
echo
echo "storage_image/c5_slave.bin  <-  sdk/c5 @ $pin  ($size bytes), stamped in c5_slave.commit"
echo "Send it straight to the C5:    python tools/kcli.py -p PORT install $image --dst c5"
echo "Or flash it onto the board:    idf.py -p PORT storage-flash"
echo "  then, on the console:        install src=\"fs:c5_slave.bin\" dst=c5"
