#!/bin/sh
# boot.sh - Upload and launch AIPC DOOM via usbboot.
#
# Flow:
#   1. DDR initialization: poke memory-mapped registers to bring up DRAM.
#   2. Upload doom.bin to DDR_DOOM_BASE.
#   3. Upload DOOM1.WAD to DDR_WAD_BASE.
#   4. Execute doom.bin.
#
# Requires: uv run ak7802-usbboot (from tools/usbboot) on PATH or in venv.
# Usage:    ./boot.sh

set -e

DOOM_BIN="doom.bin"
WAD="wad/DOOM1.WAD"

DDR_DOOM_BASE=0x30000000   # start of DDR
DDR_WAD_BASE=0x30900000    # 9 MB in; leaves ~7 MB for doom binary + heap + stack

if [ ! -f "$DOOM_BIN" ]; then
    echo "error: $DOOM_BIN not found - run 'make' first"
    exit 1
fi

if [ ! -f "$WAD" ]; then
    echo "error: $WAD not found - place DOOM1.WAD in doom/wad/"
    exit 1
fi

# Mirrors the register script embedded in the nboot image header and executed
# by the bootrom for normal boots.  See docs/nboot/boot-flow.md for details.
#
# USB round-trip latency per poke (~1 ms) far exceeds the DELAY tick counts in
# the original script, so no explicit sleep is needed between individual pokes.
# A brief sleep is added after the clock config group to be safe.
#
# v1.58.2 / v1.88 note: this script uses v1.58.2 timing (0x2002D008 = 0x00057C58).
# For v1.88 hardware add '--addr 0x20026000 --value 0x30200433' after the first
# sleep and change the last poke value to 0x00037C58.

echo "DDR init..."

uv run ak7802-usbboot poke --addr 0x080000DC --value 0x00000000  # SYSCTRL reset
uv run ak7802-usbboot poke --addr 0x08000004 --value 0x0000D000  # SYSCTRL clock config
sleep 0.1                                                         # allow clocks to stabilize
uv run ak7802-usbboot poke --addr 0x08000064 --value 0x08000000  # SYSCTRL memory config
uv run ak7802-usbboot poke --addr 0x080000A8 --value 0x04000000  # SYSCTRL memory config
uv run ak7802-usbboot poke --addr 0x2002D004 --value 0x0F506B95  # DDR controller timing
sleep 0.1                                                         # timing settle
uv run ak7802-usbboot poke --addr 0x2002D000 --value 0x40170000  # DDR init sequence
uv run ak7802-usbboot poke --addr 0x2002D000 --value 0x40120400
uv run ak7802-usbboot poke --addr 0x2002D000 --value 0x40104000
uv run ak7802-usbboot poke --addr 0x2002D000 --value 0x40100123
uv run ak7802-usbboot poke --addr 0x2002D000 --value 0x40120400
uv run ak7802-usbboot poke --addr 0x2002D000 --value 0x40110000
uv run ak7802-usbboot poke --addr 0x2002D000 --value 0x40110000
uv run ak7802-usbboot poke --addr 0x2002D000 --value 0x40100023
uv run ak7802-usbboot poke --addr 0x2002D000 --value 0x60170000  # DDR controller enable
uv run ak7802-usbboot poke --addr 0x2002D008 --value 0x00057C58  # DDR refresh timing (v1.58.2)

echo "Uploading doom.bin -> $DDR_DOOM_BASE..."
uv run ak7802-usbboot write "$DOOM_BIN" --addr "$DDR_DOOM_BASE"

echo "Uploading DOOM1.WAD -> $DDR_WAD_BASE..."
uv run ak7802-usbboot write "$WAD" --addr "$DDR_WAD_BASE"

echo "Executing at $DDR_DOOM_BASE..."
uv run ak7802-usbboot exec --addr "$DDR_DOOM_BASE"
