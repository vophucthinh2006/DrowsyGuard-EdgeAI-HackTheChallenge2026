#!/usr/bin/env bash
# =========================================================================
# FRDM-MCXN947 (dms-sim-mcxn947)  -  build (west/CMake) + flash over
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
# Copied from ../vcs-mcxn947/build.sh (same probe/CMake gotchas, same shared
# NPX_Workspace toolchain) with the app/.elf name changed and one addition:
# PROBE_UID, because a two-board CAN bring-up session has *two* MCU-Link
# probes plugged in at once, and the original's "just pick the first
# MCU-LINK by product string" logic can't tell them apart anymore. Find each
# board's UID with `pyocd list` (or `west build ... && ./build.sh flash`
# once with only one board plugged in, then check the printed UID) and pin
# it per board, e.g.:
#   PROBE_UID=<uid-of-second-board> SERIAL_PORT=/dev/ttyACM1 ./build.sh flash
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
# The SDK/west/pyOCD toolchain this project builds against -- shared with
# vcs-mcxn947, touch_rgb, wifi_sensing_npu on this machine, not part of this
# git repo. See ../vcs-mcxn947/README.md for the full setup story.
WS_ROOT="${NPX_WORKSPACE:-$HOME/embedded/NPX_Workspace}"
SDK_WEST_ROOT="$WS_ROOT/mcuxsdk"                # holds .west/
BUILD_DIR="$APP_DIR/build"
VENV_BIN="$WS_ROOT/tools/westenv/bin"

export ARMGCC_DIR="${ARMGCC_DIR:-/usr}"
export PATH="$VENV_BIN:$PATH"

# ---- helpers ------------------------------------------------------------
say() { printf '\033[1;36m>> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31m!! %s\033[0m\n' "$*" >&2; exit 1; }

find_elf() {
	local f="$BUILD_DIR/dms_sim_mcxn947_${CORE}.elf"
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
		say "To drop the sudo, install the udev rule (see ../vcs-mcxn947/README.md, 'First-time setup / probe permissions')."
		runner=(sudo env HOME="$HOME" "$VENV_BIN/pyocd")
	fi

	# PROBE_UID pins a specific probe when two boards (and two MCU-Links)
	# are plugged in at once -- without it, both this script and
	# vcs-mcxn947's would silently race for "whichever MCU-LINK comes
	# first" and you could flash the wrong board.
	local uid="${PROBE_UID:-}"
	if [ -z "$uid" ]; then
		uid="$(mcu_link_uid "${runner[@]}")"
	fi
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
	say "Flashing $elf -> $TARGET via MCU-Link (CMSIS-DAP)${PROBE_UID:+ [PROBE_UID=$PROBE_UID]}"
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
