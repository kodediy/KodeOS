#!/usr/bin/env bash
#
# A TODO, FIXME, HACK or XXX that a change adds must name the issue that will
# close it - `#42` or `kodediy/kode-sdk#42` - or it is a promise nobody is
# tracking. Only the lines the change adds are judged: what was already there
# is not this change's to answer for.
#
# The caller fetches <base>; this reads the repository it is run in.
#
#   usage: tools/check_todo_refs.sh <base>
#   exit:  0 every added marker names an issue, 1 one does not (each listed)
set -uo pipefail

base="${1:?usage: tools/check_todo_refs.sh <base>}"

# This script and its test spell the markers out on purpose. A diff that
# cannot be made - a base that is not in this clone - is a failure, not a
# change with nothing in it.
if ! diff="$(git diff -U0 --no-color "$base"...HEAD -- . \
                 ':(exclude)tools/check_todo_refs.sh' \
                 ':(exclude)tools/tests/test_check_todo_refs.sh')"; then
    echo "::error::could not diff against $base" >&2
    exit 1
fi

# The issue goes right after the marker - TODO(#42), TODO #42, TODO:
# kodediy/kode-sdk#42 - so a number elsewhere on the line (a colour, a count)
# does not pass for one.
found="$(printf '%s\n' "$diff" | awk '
             /^\+\+\+ / { file = substr($0, 7); next }
             /^@@ /     { split($3, a, ","); line = substr(a[1], 2) + 0; next }
             /^\+/      {
                 if ($0 ~ /(^|[^A-Za-z])(TODO|FIXME|HACK|XXX)([^A-Za-z]|$)/ &&
                     $0 !~ /(TODO|FIXME|HACK|XXX)[(: ]*([A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+)?#[0-9]+/) {
                     printf "%s:%d: %s\n", file, line, substr($0, 2)
                 }
                 line++
             }')"

if [ -n "$found" ]; then
    echo "::error::a marker this change adds names no issue; add one, e.g. TODO(#42)" >&2
    printf '%s\n' "$found" >&2
    exit 1
fi
exit 0
