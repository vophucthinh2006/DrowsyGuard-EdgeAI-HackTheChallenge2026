#!/usr/bin/env bash
# =========================================================================
# FRDM-MCXN947 (drowsyguard_vcs)  -  build (west/CMake) + flash over
# MCU-Link (CMSIS-DAP)
#
#   ./build.sh            build, then flash
#   ./build.sh build      build only
#   ./build.sh rebuild    clean build from scratch
#   ./build.sh flash      flash the last build
#   ./build.sh clean      remove build/
#   ./build.sh erase      mass-erase the chip (recovery)
#   ./build.sh reset      reset the board without reflashing
#   ./build.sh monitor    open the serial console
#
# Copied from NPX_Workspace/wifi_sensing_npu/build.sh (same probe/CMake
# gotchas) with the app name/.elf name changed and one more difference: this
# project's *source* lives inside this git repo, but the MCUXpresso SDK
# checkout + west venv + CMSIS packs it builds against (~4+ GB) intentionally
# do NOT — they stay in the shared local toolchain workspace
# (NPX_WORKSPACE below), the same one every other MCXN947 project on this
# machine (touch_rgb, wifi_sensing_npu) uses. Point NPX_WORKSPACE at that
# checkout if it isn't at the default path. See that file's comments for why
# each step is written the way it is; not repeated here.
# =========================================================================
set -euo pipefail

# ---- config -------------------------------------------------------------
BOARD="frdmmcxn947"
CORE="cm33_core0"
TARGET="mcxn947"              # pyOCD target name (from the CMSIS pack)
TOOLCHAIN="armgcc"
SERIAL_PORT="${SERIAL_PORT:-/dev/ttyACM0}"
BAUD=115200

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The SDK/west/pyOCD toolchain this project builds against — not part of
# this git repo, see the header comment above.
WS_ROOT="${NPX_WORKSPACE:-$HOME/embedded/NPX_Workspace}"
SDK_WEST_ROOT="$WS_ROOT/mcuxsdk"                # holds .west/
BUILD_DIR="$APP_DIR/build"
VENV_BIN="$WS_ROOT/tools/westenv/bin"

# The MCUXpresso CMake toolchain file reads $ARMGCC_DIR/bin/arm-none-eabi-*
export ARMGCC_DIR="${ARMGCC_DIR:-/usr}"
# west extension commands re-invoke "west" by name, so it must be on PATH
export PATH="$VENV_BIN:$PATH"

# ---- helpers ------------------------------------------------------------
say() { printf '\033[1;36m>> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31m!! %s\033[0m\n' "$*" >&2; exit 1; }

find_elf() {
	local f="$BUILD_DIR/drowsyguard_vcs_${CORE}.elf"
	[ -f "$f" ] && { echo "$f"; return 0; }
	f="$(find "$BUILD_DIR" -maxdepth 1 -name '*.elf' | head -1)"
	[ -n "$f" ] && { echo "$f"; return 0; }
	return 1
}

mcu_link_uid() {
	"$@" json --probes 2>/dev/null | "$VENV_BIN/python" -c '
import json, sys
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
for board in data.get("boards", []):
    if "MCU-LINK" in (board.get("product_name") or "").upper():
        print(board.get("unique_id", ""))
        break
'
}

pyocd_run() {
	local runner=("$VENV_BIN/pyocd")

	if ! "$VENV_BIN/pyocd" list 2>/dev/null | grep -qi "cmsis-dap"; then
		say "Probe not accessible as your user - using sudo."
		say "To drop the sudo, install the udev rule (see $WS_ROOT/touch_rgb/README.md, 'Probe permissions')."
		runner=(sudo env HOME="$HOME" "$VENV_BIN/pyocd")
	fi

	local uid; uid="$(mcu_link_uid "${runner[@]}")"
	if [ -n "$uid" ]; then
		"${runner[@]}" "$@" -u "$uid"
	else
		"${runner[@]}" "$@"
	fi
}

# ---- actions ------------------------------------------------------------
do_build() {
	[ -d "$SDK_WEST_ROOT/.west" ] || die "west workspace not found at $SDK_WEST_ROOT"
	say "Building $APP_DIR for $BOARD ($CORE)"
	( cd "$SDK_WEST_ROOT" && \
	  west build -b "$BOARD" "$APP_DIR" --toolchain "$TOOLCHAIN" \
	             -Dcore_id="$CORE" -d "$BUILD_DIR" "$@" )
	local elf; elf="$(find_elf)" || die "build finished but no .elf found"
	say "Built: $elf ($(du -h "$elf" | cut -f1))"
}

do_flash() {
	local elf; elf="$(find_elf)" || die "nothing to flash - run ./build.sh build first"
	say "Flashing $elf -> $TARGET via MCU-Link (CMSIS-DAP)"
	pyocd_run flash -t "$TARGET" "$elf"
	say "Done. Board reset and running."
}

do_erase() {
	say "Mass-erasing $TARGET"
	pyocd_run erase -t "$TARGET" --chip
}

do_reset() {
	say "Resetting board"
	pyocd_run reset -t "$TARGET"
}

do_monitor() {
	command -v tio     >/dev/null && exec tio "$SERIAL_PORT"
	command -v picocom >/dev/null && exec picocom -b "$BAUD" "$SERIAL_PORT"
	command -v screen  >/dev/null && exec screen "$SERIAL_PORT" "$BAUD"
	die "install tio, picocom or screen to use monitor"
}

# ---- dispatch -----------------------------------------------------------
case "${1:-all}" in
	build)   shift; do_build "$@";;
	rebuild) shift; rm -rf "$BUILD_DIR"; do_build "$@";;
	flash)   do_flash;;
	erase)   do_erase;;
	reset)   do_reset;;
	clean)   say "removing $BUILD_DIR"; rm -rf "$BUILD_DIR";;
	monitor) do_monitor;;
	all)     do_build; do_flash;;
	*)       die "unknown command '$1' (build|rebuild|flash|erase|reset|clean|monitor)";;
esac
