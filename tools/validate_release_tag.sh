#!/usr/bin/env bash
#
# One tag format, checked in one place: vMAJOR.MINOR.PATCH, the same
# two-or-more-digit-group scheme kodeOS's own PROJECT_VER already uses (see
# CMakeLists.txt and https://docs.kode.diy/internals/release-pipeline). The
# release workflow uses this both to reject a malformed tag before
# spending build minutes, and to derive the exact string that goes into
# `-DPROJECT_VER=...` - one source of truth for "what does this tag mean".
#
#   usage: tools/validate_release_tag.sh <tag>
#   stdout: the version (tag without the leading "v"), on success only
#   exit:   0 valid, 1 malformed (message on stderr)
set -euo pipefail

tag="${1:-}"

if [[ ! "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "error: '$tag' is not a valid release tag (expected vMAJOR.MINOR.PATCH, e.g. v00.00.02)" >&2
    exit 1
fi

echo "${tag#v}"
