#!/bin/sh
# Upload AIPC DOOM over USB boot and start it.
#
# Steps:
#   1. Initialize DDR.
#   2. Upload doom*.bin to DDR_DOOM_BASE.
#   3. Upload the selected IWAD to DDR_WAD_BASE.
#   4. Execute doom*.bin.
#
# Needs aipc-ddr-init and ak7802-usbboot, both under tools/. This script runs
# them with `uv run` from the workspace root.
#
# Usage: ./boot.sh [WAD_VERSION] [FIRMWARE]
#
# WAD_VERSION matches the WAD= parameter of the Makefile:
#   shareware (default)  DOOM1.WAD,     runs doom.bin
#   doom1                DOOM.WAD,      runs doom-doom1.bin
#   doom2                DOOM2.WAD,     runs doom-doom2.bin
#   tnt                  TNT.WAD,       runs doom-tnt.bin
#   plutonia             PLUTONIA.WAD,  runs doom-plutonia.bin
#
# FIRMWARE is the DDR init firmware version, 1.58.2 or 1.88 (default).
# See tools/aipc-ddr-init/README.md.

set -e

DDR_DOOM_BASE=0x30200000   # above the shared log pool, see baremetal/README.md
DDR_WAD_BASE=0x30B00000    # above the doom image, heap and stack

WAD_VERSION="${1:-shareware}"
FIRMWARE="${2:-1.88}"

find_wad() {
    local name="$1"
    local found
    found=$(find wad -maxdepth 1 -iname "$name" 2>/dev/null | head -1)
    echo "$found"
}

case "$WAD_VERSION" in
    shareware)
        DOOM_BIN="doom.bin"
        WAD=$(find_wad "DOOM1.WAD")
        WAD_DISPLAY="DOOM1.WAD"
        ;;
    doom1)
        DOOM_BIN="doom-doom1.bin"
        WAD=$(find_wad "DOOM.WAD")
        WAD_DISPLAY="DOOM.WAD"
        ;;
    doom2)
        DOOM_BIN="doom-doom2.bin"
        WAD=$(find_wad "DOOM2.WAD")
        WAD_DISPLAY="DOOM2.WAD"
        ;;
    tnt)
        DOOM_BIN="doom-tnt.bin"
        WAD=$(find_wad "TNT.WAD")
        WAD_DISPLAY="TNT.WAD"
        ;;
    plutonia)
        DOOM_BIN="doom-plutonia.bin"
        WAD=$(find_wad "PLUTONIA.WAD")
        WAD_DISPLAY="PLUTONIA.WAD"
        ;;
    *)
        echo "error: unknown WAD version '$WAD_VERSION'"
        echo "supported: shareware doom1 doom2 tnt plutonia"
        exit 1
        ;;
esac

if [ ! -f "$DOOM_BIN" ]; then
    echo "error: $DOOM_BIN not found - run 'make WAD=$WAD_VERSION' first"
    exit 1
fi

if [ -z "$WAD" ] || [ ! -f "$WAD" ]; then
    echo "error: $WAD_DISPLAY not found in doom/wad/ - copy it from your Steam installation"
    exit 1
fi

echo "DDR init..."
uv run aipc-ddr-init --firmware "$FIRMWARE"

echo "sending $DOOM_BIN..."
uv run ak7802-usbboot write "$DOOM_BIN" --addr "$DDR_DOOM_BASE"

echo "sending $WAD..."
uv run ak7802-usbboot write "$WAD" --addr "$DDR_WAD_BASE"

uv run ak7802-usbboot exec --addr "$DDR_DOOM_BASE"
