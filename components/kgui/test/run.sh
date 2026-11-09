#!/bin/sh
# kgui's pure parts, compiled and run on the host. Needs the SDK's headers and
# ESP-IDF's Unity (IDF_PATH), no board and no idf.py.
set -e
cd "$(dirname "$0")"
sdk=../../../sdk/components
unity="$IDF_PATH/components/unity/unity/src"
out="${TMPDIR:-/tmp}/kode_kgui_tests"
mkdir -p "$out"

inc="-I.. -I../include -I../screens -I$unity \
 -I$sdk/kui/core/include -I$sdk/kdefs/include -I$sdk/klog/include -I$sdk/kdrv/apps/include \
 -I$sdk/khal/nvs/include -I$sdk/khal/sync/include -I$sdk/khal/system/include"

# The SDK's and Unity's own sources are built as they are, without -Werror.
cc -std=c11 $inc -c "$unity/unity.c" -o "$out/unity.o"
cc -std=c11 $inc -c "$sdk/kdrv/apps/src/kapp_cat.c" -o "$out/kapp_cat.o"

# One host binary for each pure part: the declared cases are counted against
# the ones that ran, so a case that is declared and never run fails the script.
run_suite() {
    name=$1
    shift
    cc -std=c11 -Wall -Wextra -Werror $inc "$@" \
        "$out/unity.o" -o "$out/$name"
    # A case that is declared and never run would pass in silence.
    declared=$(grep -c '^void test_' "test_$name.c")
    result=$("$out/$name" < /dev/null) || { echo "$result"; exit 1; }
    echo "$result"
    ran=$(echo "$result" | awk '/[0-9]+ Tests [0-9]+ Failures/ { print $1; exit }')
    if [ "${ran:-0}" != "$declared" ]; then
        echo "FAIL  $name: $declared cases declared, ${ran:-0} ran"
        exit 1
    fi
}

run_suite kgui_group test_kgui_group.c ../kgui_group.c kgui_group_stubs.c "$out/kapp_cat.o"
run_suite kgui_dtext test_kgui_dtext.c ../kgui_dtext.c
# The scenes' English words are a table: no stubs.
run_suite kgui_scene_label test_kgui_scene_label.c ../kgui_scene_label.c
# The arc runs the edge's path: its numbers come from kgui_edge.h.
run_suite kgui_off test_kgui_off.c ../screens/kgui_off.c
# The edge's curves are pure: no LVGL, no stubs.
run_suite kgui_edge test_kgui_edge.c ../kgui_edge.c
# The battery's liquid is pure too.
run_suite kgui_fluid test_kgui_fluid.c ../kgui_fluid.c -lm
# The landing after Welcome: its mark, its clock and the ring's path.
run_suite kgui_welcome test_kgui_welcome.c ../kgui_welcome.c -lm

# The system cards' words read two enums and one call of kaos.h, whose QP and
# FreeRTOS the host has not got: they are cut out of the SDK's own sources.
kaos="$sdk/kaos/core/include/kaos.h"
boot="$sdk/kaos/boot/src/boot_ao.c"
mkdir -p "$out/kaos"
{
    printf '#ifndef KAOS_H\n#define KAOS_H\n#include <stdint.h>\n'
    awk '/^typedef enum \{/ { buf = ""; grab = 1 }
         grab { buf = buf $0 "\n" }
         grab && /^\} / { if ($0 ~ /^\} kboot_(app_end|reset_step)_t;/) printf "%s", buf; grab = 0 }' "$kaos"
    printf 'const char *boot_reset_step_name(uint8_t step);\n#endif\n'
} > "$out/kaos/kaos.h"
{
    printf '#include "kaos.h"\n'
    awk '/^const char \*boot_reset_step_name\(uint8_t step\)$/ { grab = 1 }
         grab { print }
         grab && /^\}$/ { exit }' "$boot"
} > "$out/boot_reset_step.c"
if [ "$(grep -c '^} kboot_' "$out/kaos/kaos.h")" != 2 ] \
   || ! grep -q '^const char \*boot_reset_step_name' "$out/boot_reset_step.c"; then
    echo "FAIL  kgui_reset: kaos.h or boot_ao.c no longer has what the cut expects"
    exit 1
fi
run_suite kgui_reset -I"$out/kaos" test_kgui_reset.c ../kgui_syscard.c "$out/boot_reset_step.c"
# The critical battery card's latch, in the same file.
run_suite kgui_crit -I"$out/kaos" test_kgui_crit.c ../kgui_syscard.c "$out/boot_reset_step.c"

# What a cable is doing, in words: the chip's vocabulary and no chip.
run_suite kgui_charge -I"$sdk/kdrv/power/include" test_kgui_charge.c ../kgui_charge.c

# When a pack is given up for its canvas.
run_suite kgui_pack_fit test_kgui_pack_fit.c ../kgui_pack_fit.c

# The radios' rows: their words read kdrv_wifi.h's reasons and nothing else.
run_suite kgui_wifi -I"$sdk/kdrv/radio/include" test_kgui_wifi.c ../kgui_wifi.c
# Kody's line drawing at Rest is pure too.
run_suite kgui_rest test_kgui_rest.c ../kgui_rest.c

# The shelf of recents reads the rail's numbers (kgui_motion.h), whose headers
# name the board's glass and the display's frame.
run_suite kgui_shelf -I"$sdk/kbsp/board/include" -I"$sdk/kdrv/display/include" \
    -I"$sdk/kui/drag/include" test_kgui_shelf.c ../kgui_shelf.c -lm
# Where a list stands.
run_suite kgui_motion -I"$sdk/kbsp/board/include" -I"$sdk/kdrv/display/include" \
    -I"$sdk/kui/drag/include" test_kgui_motion.c ../kgui_motion.c -lm
