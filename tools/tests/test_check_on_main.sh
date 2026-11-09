#!/usr/bin/env bash
# Exercises tools/check_on_main.sh against a scratch repository: a commit on
# the branch passes, a commit on a side branch is refused, and so is a commit
# that does not exist.
#   usage: tools/tests/test_check_on_main.sh
#   exit:  0 all cases passed, 1 something regressed
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
script="$PWD/tools/check_on_main.sh"

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

g() { git -C "$scratch" -c user.name=t -c user.email=t@t -c commit.gpgsign=false "$@"; }
g init -q -b main
g commit -q --allow-empty -m one
on_main="$(g rev-parse HEAD)"
g checkout -q -b side
g commit -q --allow-empty -m two
off_main="$(g rev-parse HEAD)"
g checkout -q main

fail=0
check() {   # check <name> <commit> <expected_exit> [<expected_stderr_fragment>]
    local name="$1" commit="$2" expected="$3" fragment="${4:-}"
    local err rc
    err="$(cd "$scratch" && "$script" "$commit" main 2>&1 >/dev/null)"
    rc=$?
    if [ "$rc" -ne "$expected" ]; then
        printf 'FAIL  %s: expected exit %s, got %s\n' "$name" "$expected" "$rc"
        fail=1
    elif [ -n "$fragment" ] && [[ "$err" != *"$fragment"* ]]; then
        printf 'FAIL  %s: the refusal does not say %q: %s\n' "$name" "$fragment" "$err"
        fail=1
    else
        printf 'ok    %s\n' "$name"
    fi
}

check "a commit on the branch passes"        "$on_main"  0
check "a commit off the branch is refused"   "$off_main" 1 "${off_main:0:12}"
check "a commit that does not exist is refused" \
      "0123456789012345678901234567890123456789" 1 "not a commit"

if [ "$fail" -eq 0 ]; then
    echo "all cases passed"
else
    echo "one or more cases failed"
fi
exit "$fail"
