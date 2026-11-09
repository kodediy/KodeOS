#!/usr/bin/env bash
#
# The build's four results as one Markdown table: the image against the
# mailbox ceiling, internal RAM against its budget, the Active Objects the
# image starts, MISRA against its baseline.
# CI writes it to the job summary and to one comment on the pull request, so
# nobody has to open a log to see what a change cost. It only formats: the
# checks themselves (check_os_size.sh, check_ram.sh, check_misra.sh) decide
# pass or fail, and their output, saved to <dir>, is all this reads.
#
#   usage: tools/ci_report.sh <dir>   (size.txt, ram.txt, starts.txt, misra.txt inside)
#   stdout: the report, Markdown; exit 0 always
set -uo pipefail

dir="${1:?usage: tools/ci_report.sh <dir>}"

# The comment's identity: CI finds the comment it wrote by this line and edits
# it in place, so a pull request carries one report, the latest.
echo '<!-- kodeos-ci-report -->'
echo '### kodeOS build'
echo
echo '| Check | Result |'
echo '|---|---|'

# row <title> <file> <pattern of the summary line>
row() {
    local line=""
    if [ -s "$dir/$2" ]; then
        line="$(sed 's/\x1b\[[0-9;]*m//g' "$dir/$2" | grep -E "$3" | tail -1)"
        if [ -z "$line" ]; then
            line="failed, see below"
        fi
    else
        line="did not run"
    fi
    printf '| %s | %s |\n' "$1" "$line"
}

row "Image size"   size.txt  '^kodeos\.bin [0-9]+ of [0-9]+ bytes'
row "Internal RAM" ram.txt   'statics of [0-9]+ bytes or more'
row "Starts"       starts.txt '^starts: [0-9]+ Active Objects'
row "MISRA"        misra.txt '^MISRA: [0-9]+ findings'

# The lines that say why something failed, as the checks wrote them.
why="$(cat "$dir"/size.txt "$dir"/ram.txt "$dir"/starts.txt "$dir"/misra.txt 2>/dev/null \
       | sed 's/\x1b\[[0-9;]*m//g' | grep -E '^(error|FAIL|::error|::warning)|^ {5,}' || true)"
if [ -n "$why" ]; then
    echo
    echo '```'
    printf '%s\n' "$why"
    echo '```'
fi
exit 0
