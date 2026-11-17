#!/usr/bin/env bash
#
# The tools' own tests: everything under tools/ that needs neither a board nor
# ESP-IDF.
#
# This script exists because those tests were not being run. Each one was
# written for a real regression, documented in tools/README.md, and then left
# to be invoked by hand - which meant, in practice, invoked when somebody
# remembered. `tools/tests/test_kmirror.py` is the sharpest case: it exists because
# kmirror.py's two transports drifted apart TWICE, and the mechanism meant to
# stop the third time was itself only run on request.
#
# A third runner rather than a bigger one. tools/host_tests.sh covers the C
# suites and tools/check_layers.sh covers the include rules; CI calls all
# three by name, so a failure says which class of thing broke without anybody
# reading further.
#
#   usage: tools/tool_tests.sh
#   exit:  0 every test passed, 1 something failed or is not being run
#
set -uo pipefail
cd "$(dirname "$0")/.."

# Every test here imports the module it is testing, so a stale
# tools/__pycache__ would have this runner reporting on code that is not in
# the tree. Python invalidates a .pyc on (mtime, size) at one-second
# granularity, and two edits inside the same second with the same length -
# a one-character fix, say - slip straight through that check. Measured
# exactly that way, on this runner's first real use. CI checks out clean and
# would never see it; a workstation sees it on the day it matters most.
export PYTHONDONTWRITEBYTECODE=1

# Every test, spelled out. The completeness check below is what keeps this
# list honest - see its own comment.
TESTS=(
    "tools/tests/test_kble.py"
    "tools/tests/test_kcli.py"
    "tools/tests/test_kmirror.py"
    "tools/tests/test_kappdata.py"
    "tools/tests/test_mkapps.py"
    "tools/tests/test_kpayload.py"
    "tools/tests/test_assemble_release.sh"
    "tools/tests/test_validate_release_tag.sh"
    "tools/tests/test_check_on_main.sh"
    "tools/tests/test_ci_report.sh"
    "tools/tests/test_check_todo_refs.sh"
    "tools/tests/test_factory.py"
    # The station program's own two, beside it (tools/factory/README.md).
    "tools/factory/test_dot_pads.py"
    "tools/factory/test_dot_parallel.py"
    # The SDK's, run from here because the SDK has no runner of its own for
    # its tools and kodeOS's build is judged by this script.
    "sdk/tools/test_check_ram.sh"
    "sdk/tools/test_check_misra.sh"
    "sdk/tools/test_check_starts.sh"
    "sdk/tools/test_check_wire_twins.sh"
)

fail=0

# A test file that exists and is in nobody's list is exactly the hole this
# script was written to close, one level up: it would sit in the tree looking
# like coverage and never run. So the list is checked against the tree rather
# than globbed from it - globbing would make any new file silently "covered"
# and this check impossible to fail, which is the same trade host_tests.sh
# refuses when it counts declared cases against cases that ran.
for found in tools/tests/test_*; do
    [ -e "$found" ] || continue
    listed=0
    for t in "${TESTS[@]}"; do
        [ "$t" = "$found" ] && listed=1
    done
    if [ "$listed" -eq 0 ]; then
        printf '\033[31mFAIL\033[0m  %s exists and is not in this runner - add it\n' \
               "$found"
        fail=1
    fi
done

# ESP-IDF's Python has no Pillow, and the two tests that need it fail
# without naming it, so the runner names it first.
if ! python3 -c 'import PIL' >/dev/null 2>&1; then
    printf '\033[31mFAIL\033[0m  Pillow is missing, and test_kappdata.py and test_factory.py need it: python3 -m pip install pillow\n'
    fail=1
fi

for t in "${TESTS[@]}"; do
    if [ ! -f "$t" ]; then
        printf '\033[31mFAIL\033[0m  %s is listed here and not in the tree\n' "$t"
        fail=1
        continue
    fi

    case "$t" in
        *.py) out=$(python3 "$t" 2>&1) ;;
        *)    out=$(bash "$t" 2>&1)    ;;
    esac
    rc=$?

    if [ "$rc" -eq 0 ]; then
        printf '\033[32mok\033[0m    %s\n' "$t"
    else
        printf '\033[31mFAIL\033[0m  %s (exit %d)\n' "$t" "$rc"
        # The whole output, not a tail: these suites are small, and a
        # truncated failure is one more round trip for whoever is reading CI.
        printf '%s\n' "$out" | sed 's/^/      /'
        fail=1
    fi
done

if [ "$fail" -eq 0 ]; then
    printf '\n\033[32mok\033[0m    %d tool tests, all passing\n' "${#TESTS[@]}"
else
    printf '\n\033[31mThe tools are not covered by what they claim.\033[0m\n'
fi
exit "$fail"
