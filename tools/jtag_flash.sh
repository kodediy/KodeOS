#!/usr/bin/env bash
#
# Flash over JTAG without going through the extension's OpenOCD server.
#
# The extension does not start a fresh OpenOCD per flash: it drives a
# long-lived one over the TCL port (6666), and that server outlives the board.
# The Kode Dot re-enumerates on every reset, so sooner or later the USB handle
# behind the server is stale - it still holds the gdb port, and it can no
# longer reach the chip. The flash then dies in init, and no amount of
# retrying helps until the server is killed by hand.
#
# So this takes the other path: free the adapter, run one OpenOCD that
# programs the images and shuts down, leave nothing listening.
#
# The images and their offsets are read from the build, never spelled out
# here - the partition table is free to move without this script rotting.
#
#   usage: tools/jtag_flash.sh [build-dir]     (default: build)
#   exit:  0 flashed and verified, 1 nothing to flash, 2 the adapter stayed busy

set -euo pipefail

BUILD_DIR="${1:-build}"
ARGS_JSON="$BUILD_DIR/flasher_args.json"
OCD_CFG="${OPENOCD_CFG:-board/esp32p4-builtin.cfg}"

if [ ! -f "$ARGS_JSON" ]; then
    echo "no $ARGS_JSON - build the project first" >&2
    exit 1
fi

if ! command -v openocd >/dev/null 2>&1; then
    echo "openocd is not on PATH - source the IDF export script first" >&2
    exit 1
fi

# Whatever listens on the gdb port owns the adapter, and it cannot be shared.
holders() { lsof -t -nP -i :3333 2>/dev/null || true; }

PIDS="$(holders)"
if [ -n "$PIDS" ]; then
    echo "the JTAG adapter is held, freeing it:"
    # shellcheck disable=SC2086
    ps -o pid,ppid,lstart,command -p $PIDS | tail -n +2
    # shellcheck disable=SC2086
    kill $PIDS 2>/dev/null || true
    sleep 2
    PIDS="$(holders)"
    if [ -n "$PIDS" ]; then
        # shellcheck disable=SC2086
        kill -9 $PIDS 2>/dev/null || true
        sleep 1
    fi
fi

if [ -n "$(holders)" ]; then
    echo "port 3333 is still held; flash aborted" >&2
    exit 2
fi

# One -c per image, in ascending offset order, straight from the build.
OCD_ARGS=()
while IFS= read -r line; do
    OCD_ARGS+=("$line")
done < <(python3 - "$ARGS_JSON" "$BUILD_DIR" <<'PY'
import json, sys

args_json, build_dir = sys.argv[1], sys.argv[2]
files = json.load(open(args_json, encoding="utf-8"))["flash_files"]
for offset, name in sorted(files.items(), key=lambda kv: int(kv[0], 16)):
    print("-c")
    print(f"program_esp {build_dir}/{name} {offset} verify")
PY
)

SCRIPTS_ARG=()
if [ -n "${OPENOCD_SCRIPTS:-}" ]; then
    SCRIPTS_ARG=(-s "$OPENOCD_SCRIPTS")
fi

# 'shutdown', not 'exit': the latter is deprecated outside telnet/TCL sessions.
openocd "${SCRIPTS_ARG[@]}" -f "$OCD_CFG" \
        "${OCD_ARGS[@]}" \
        -c "reset run" \
        -c "shutdown"

if [ -n "$(holders)" ]; then
    echo "warning: something is listening on 3333 again" >&2
fi

echo "flashed over JTAG; nothing left listening"
