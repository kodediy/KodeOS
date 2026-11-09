#!/usr/bin/env bash
# Exercises tools/ci_report.sh against canned outputs of the four build
# checks: a green build reports each number on its own row, a red one quotes
# the lines that say why, and a check that left no output says so.
#   usage: tools/tests/test_ci_report.sh
#   exit:  0 all cases passed, 1 something regressed
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
script="$PWD/tools/ci_report.sh"

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

fail=0
expect() {   # expect <name> <haystack> <needle>
    if [[ "$2" == *"$3"* ]]; then
        printf 'ok    %s\n' "$1"
    else
        printf 'FAIL  %s: no %q in:\n%s\n' "$1" "$3" "$2"
        fail=1
    fi
}

mkdir -p "$scratch/green"
printf 'kodeos.bin 2621440 of 4190208 bytes, 1568768 left (37%%)\n' > "$scratch/green/size.txt"
printf 'kodeos.elf 9 statics of 2048 bytes or more in internal RAM, all listed\n' > "$scratch/green/ram.txt"
printf 'starts: 20 Active Objects and 6 door set-ups, each beside what it starts\n' > "$scratch/green/starts.txt"
printf 'MISRA: 812 findings in 97 files, none past the baseline\n' > "$scratch/green/misra.txt"
out="$("$script" "$scratch/green")"
expect "the image row carries the size line" "$out" "| Image size | kodeos.bin 2621440 of 4190208 bytes, 1568768 left (37%) |"
expect "the RAM row carries the statics line" "$out" "| Internal RAM | kodeos.elf 9 statics"
expect "the starts row carries the count" "$out" "| Starts | starts: 20 Active Objects and 6 door set-ups, each beside what it starts |"
expect "the MISRA row carries the count" "$out" "| MISRA | MISRA: 812 findings in 97 files, none past the baseline |"
expect "a green report has a marker for the PR comment" "$out" "<!-- kodeos-ci-report -->"

mkdir -p "$scratch/red"
printf 'kodeos.bin 2621440 of 4190208 bytes, 1568768 left (37%%)\n' > "$scratch/red/size.txt"
printf 'kodeos.elf 9 statics of 2048 bytes or more in internal RAM, all listed\n' > "$scratch/red/ram.txt"
printf 'error: sdk/components/kaos/x.c has 3 of 8.4, the baseline allows 2:\n         sdk/components/kaos/x.c:12\n' > "$scratch/red/misra.txt"
printf 'FAIL  compiled but not in the image: AO_Touch\n' > "$scratch/red/starts.txt"
out="$("$script" "$scratch/red")"
expect "a failed check's lines are quoted" "$out" "error: sdk/components/kaos/x.c has 3 of 8.4"
expect "a failed start check is quoted" "$out" "FAIL  compiled but not in the image: AO_Touch"

mkdir -p "$scratch/empty"
out="$("$script" "$scratch/empty")"
expect "a check with no output says it did not run" "$out" "| Image size | did not run |"

if [ "$fail" -eq 0 ]; then
    echo "all cases passed"
else
    echo "one or more cases failed"
fi
exit "$fail"
