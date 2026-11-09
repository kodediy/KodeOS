#!/usr/bin/env bash
#
# The only C kodeOS owns above the SDK is kgui, whose logic is mostly
# entangled with LVGL objects; its pure parts (the group model) have a host
# test of their own. So this runs the SDK's suite, then kgui's, and says
# plainly when the submodule is not there to run.
#
# The same split as tools/check_layers.sh, for the same reason.
#
#   usage: tools/host_tests.sh
#   exit:  0 every case passed, 1 something failed
#
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

if [ ! -x sdk/tools/host_tests.sh ]; then
    # On a workstation a missing submodule is a choice; in CI it is a fetch
    # that went wrong, and a skip there would be a green tick over nothing.
    if [ "${CI:-}" = "true" ]; then
        printf '\033[31mFAIL\033[0m  the SDK submodule is not checked out in CI; '
        printf 'its host tests were not run\n'
        exit 1
    fi
    printf '\033[33mskipped\033[0m  the SDK submodule is not checked out; '
    printf 'its host tests were not run\n'
    exit 0
fi

status=0

printf -- '-- sdk --\n'
sdk/tools/host_tests.sh || status=1

# kgui's pure parts need the SDK's headers and ESP-IDF's Unity, both checked
# above; the SDK script has already refused to run without IDF_PATH.
printf -- '\n-- kgui --\n'
components/kgui/test/run.sh || status=1

exit "$status"
