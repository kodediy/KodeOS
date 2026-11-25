#!/usr/bin/env bash
#
# kodeOS must build with the radio left out. The simulator builds it that
# way, and the board's build never compiles that side of the guards, so a
# line outside its guard goes unseen until somebody builds without the radio.
# Leaving AO_Radio out takes Wi-Fi, Bluetooth, the network and AO_LowPower
# with it; the factory test needs the radio and is left out beside it.
#
# The build has a directory and a configuration of its own, written again on
# every run, so nothing an earlier run left decides what is built. It shares
# managed_components/ with the board's build: the next board build resolves
# its components again.
#
#   usage: tools/check_noradio.sh [build-dir]   (build_noradio when left out)
#   stdout: "noradio: kodeOS builds with the radio left out" when it does;
#           when it does not, the compiler's error lines, indented, and one
#           "error:" line. The whole build is in <build-dir>/noradio.log.
#   exit:   0 it builds, 1 it does not, or the configuration kept the radio
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

dir="${1:-build_noradio}"
log="$dir/noradio.log"
errors_shown=20   # The compiler's lines the report quotes; the log has them all.

mkdir -p "$dir" || exit 1
printf '%s\n' '# CONFIG_KODEOS_RADIO is not set' \
              '# CONFIG_KODE_TEST_DOT is not set' > "$dir/sdkconfig"

if ! idf.py -B "$dir" -DSDKCONFIG="$PWD/$dir/sdkconfig" reconfigure > "$log" 2>&1; then
    tail -n "$errors_shown" "$log" | sed 's/^/     /'
    echo "error: the build with the radio left out could not be configured ($log)"
    exit 1
fi

# A configuration that kept what it was told to leave out builds the board's
# own image again, and a green result here would say nothing.
kept="$(grep -E '^CONFIG_(KODEOS_RADIO|KODEOS_WIFI|KODEOS_BLE|KODE_LOWPOWER|KODE_TEST_DOT)=y' "$dir/sdkconfig" || true)"
if [ -n "$kept" ]; then
    printf '%s\n' "$kept" | sed 's/^/     /'
    echo "error: the configuration kept what the radio-less build leaves out ($dir/sdkconfig)"
    exit 1
fi

if ! idf.py -B "$dir" build >> "$log" 2>&1; then
    # The compiler colours its lines; the escape is spelt with printf so the
    # sed of every host reads it.
    esc="$(printf '\033')"
    sed "s/${esc}\[[0-9;]*[A-Za-z]//g" "$log" | grep -E 'error:|undefined reference' \
        | head -n "$errors_shown" | sed 's/^/     /'
    echo "error: kodeOS does not build with the radio left out ($log)"
    exit 1
fi

echo "noradio: kodeOS builds with the radio left out"
