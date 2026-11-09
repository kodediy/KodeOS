#!/usr/bin/env bash
# Exercises tools/validate_release_tag.sh against valid and invalid tags.
#   usage: tools/tests/test_validate_release_tag.sh
#   exit:  0 all cases passed, 1 something regressed
set -uo pipefail
cd "$(dirname "$0")/../.."

fail=0
check() {   # check <tag> <expected_exit> [<expected_stdout>]
    local tag="$1" expected_exit="$2" expected_out="${3:-}"
    local out rc
    out="$(tools/validate_release_tag.sh "$tag" 2>/dev/null)"
    rc=$?
    if [ "$rc" -ne "$expected_exit" ]; then
        printf 'FAIL  tag=%q  expected exit %s, got %s\n' "$tag" "$expected_exit" "$rc"
        fail=1
    elif [ "$expected_exit" -eq 0 ] && [ "$out" != "$expected_out" ]; then
        printf 'FAIL  tag=%q  expected stdout %q, got %q\n' "$tag" "$expected_out" "$out"
        fail=1
    else
        printf 'ok    tag=%q\n' "$tag"
    fi
}

check "v00.00.02"  0 "00.00.02"
check "v1.2.3"      0 "1.2.3"
check "v1.2"        1
check "1.2.3"       1
check "v1.2.3-rc1"  1
check ""            1

if [ "$fail" -eq 0 ]; then
    echo "all cases passed"
else
    echo "one or more cases failed"
fi
exit "$fail"
