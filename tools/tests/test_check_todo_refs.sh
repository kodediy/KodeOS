#!/usr/bin/env bash
# Exercises tools/check_todo_refs.sh on a scratch repository: a TODO added
# with an issue passes, one added without fails naming its file and line, and
# one that was already there before the base is not this change's to answer.
#   usage: tools/tests/test_check_todo_refs.sh
#   exit:  0 all cases passed, 1 something regressed
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
script="$PWD/tools/check_todo_refs.sh"

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

g() { git -C "$scratch" -c user.name=t -c user.email=t@t -c commit.gpgsign=false "$@"; }
g init -q -b main
printf 'int a; /* TODO: an old one, not ours */\n' > "$scratch/old.c"
g add old.c
g commit -q -m base
base="$(g rev-parse HEAD)"

fail=0
check() {   # check <name> <expected_exit> [<fragment>] [<base>]
    local out rc
    out="$(cd "$scratch" && "$script" "${4:-$base}" 2>&1)"
    rc=$?
    if [ "$rc" -ne "$2" ]; then
        printf 'FAIL  %s: expected exit %s, got %s: %s\n' "$1" "$2" "$rc" "$out"
        fail=1
    elif [ -n "${3:-}" ] && [[ "$out" != *"$3"* ]]; then
        printf 'FAIL  %s: the output does not say %q: %s\n' "$1" "$3" "$out"
        fail=1
    else
        printf 'ok    %s\n' "$1"
    fi
}

check "a change with no TODO passes" 0

printf 'int b; /* TODO(#42): drop once the C5 reports it */\n' > "$scratch/b.c"
printf 'int c; /* FIXME kodediy/kode-sdk#7 */\n' > "$scratch/c.c"
g add b.c c.c
g commit -q -m refs
check "a TODO or FIXME that names an issue passes" 0

printf 'int d;\nint e; /* HACK until the panel driver is fixed */\n' > "$scratch/d.c"
g add d.c
g commit -q -m bare
check "a HACK with no issue fails, naming its file and line" 1 "d.c:2"

base="$(g rev-parse HEAD)"
printf 'int f; /* TODO fix the colour #333 */\n' > "$scratch/f.c"
g add f.c
g commit -q -m loose
check "a number elsewhere on the line is not an issue" 1 "f.c:1"

check "a base that does not exist fails rather than passing" 1 "" "0123456789abcdef0123456789abcdef01234567"

if [ "$fail" -eq 0 ]; then
    echo "all cases passed"
else
    echo "one or more cases failed"
fi
exit "$fail"
