#!/usr/bin/env bash
#
# Is this commit on that branch? The release refuses a tag that sits anywhere
# but main: every commit on main has been through a pull request and main's
# own run, and a tag pushed from a side branch has been through neither.
# Also used for the C5: the commit the SDK pins must be on the C5's main.
#
# The caller fetches <ref> first; this script reads the repository it is run
# in and touches nothing.
#
#   usage: tools/check_on_main.sh <commit> <ref>
#   exit:  0 the commit is reachable from <ref>, 1 it is not (::error:: on stderr)
set -uo pipefail

commit="${1:-}"
ref="${2:-}"

if [ -z "$commit" ] || [ -z "$ref" ]; then
    echo "usage: $0 <commit> <ref>" >&2
    exit 1
fi

if ! git cat-file -e "${commit}^{commit}" 2>/dev/null; then
    echo "::error::$commit is not a commit in this repository" >&2
    exit 1
fi

if ! git merge-base --is-ancestor "$commit" "$ref"; then
    echo "::error::$commit is not on $ref; release only what $ref has been through" >&2
    exit 1
fi
exit 0
