#!/usr/bin/env bash
#
# The layer rules that are still kodeOS's own.
#
# Five of the six went with the SDK: they are about what happens *inside* it -
# where ESP-IDF stops, where QP/C stops, where pins are defined, where stdio
# stops, where the C5's RPC stops. They are checked in the SDK's own tree, by
# sdk/tools/check_layers.sh, which this script runs when the submodule is
# checked out.
#
# What is left here is the one rule that spans kodeOS rather than the SDK.
#
#   usage: tools/check_layers.sh
#   exit:  0 all clear, 1 something crossed a layer
#
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

fail=0
report() {   # report <rule> <explanation> <matches>
    if [ -n "$3" ]; then
        printf '\n\033[31mFAIL\033[0m  %s\n      %s\n\n%s\n' "$1" "$2" "$3"
        fail=1
    else
        printf '\033[32mok\033[0m    %s\n' "$1"
    fi
}

# C++ sources are read with the C ones: a rule that never opens a file
# passes it.
scan() { grep -rn --include='*.c' --include='*.h' --include='*.cpp' --include='*.hpp' \
             "$1" components main apps \
             2>/dev/null \
           | grep -v '/build/' \
           | grep -v 'managed_components/' \
           | grep -vE ':[0-9]+: *(\*|//|/\*)' \
           | { [ $# -gt 1 ] && grep -vE "$2" || cat; }; }

# ---- LVGL stops at kgui ----------------------------------------------------
# The rule is really about *threads*, not files: there is no lvgl_api_lock in
# this tree because AO_Gui is the only caller. kgui's screens are builders
# AO_Gui calls, on that same thread, and the kit they draw with is the SDK's
# (sdk/components/kui, held to the same rule by the SDK's own script). Any
# other file is a second caller and very probably a second thread.
report "lv_* only in kgui" \
  "Two threads in LVGL is the bug this rule prevents; there is no lock." \
  "$(scan '\blv_[a-z_]*(' 'components/kgui/')"

# ---- and the SDK checks itself --------------------------------------------
# The SDK's rule 11 keeps cJSON inside kui_pack.c; kgui reads Kody's pack
# through kui_pack.h and never parses JSON itself.
report "cJSON.h nowhere above the SDK" \
  "Parse a pack through kui_pack.h; cJSON recurses on the caller's stack." \
  "$(scan '#include [<"]cJSON\.h[>"]')"

if [ -x sdk/tools/check_layers.sh ]; then
    printf '\n-- sdk --\n'
    sdk/tools/check_layers.sh || fail=1
elif [ "${CI:-}" = "true" ]; then
    # On a workstation a missing submodule is a choice; in CI it is a fetch
    # that went wrong, and a skip there would be a green tick over nothing.
    printf '\n\033[31mFAIL\033[0m  the SDK submodule is not checked out in CI; its rules were not run\n'
    fail=1
else
    printf '\n\033[33mskipped\033[0m  the SDK submodule is not checked out; its own rules were not run\n'
fi

if [ "$fail" -eq 0 ]; then
    printf '\n\033[32mAll layer rules hold.\033[0m\n'
else
    printf '\n\033[31mThe cake has a hole in it.\033[0m\n'
fi
exit "$fail"
