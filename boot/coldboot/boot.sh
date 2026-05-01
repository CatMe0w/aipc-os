#!/bin/sh
set -e

COLDBOOT_BASE=0x48000240
DDR_LINUX_BASE=0x30008000

FIRMWARE="${2:-1.88}"

find_file() {
    local name="$1"
    local found
    found=$(find . -ipath "./$name" 2>/dev/null | head -1)
    echo "$found"
}

COLDBOOT_FILE=$(find_file "stub/coldboot.bin")
ZIMAGE_FILE=$(find_file "zImage")

if [ ! -f "$COLDBOOT_FILE" ]; then
    echo "error: $COLDBOOT_FILE not found - run 'cd stub && make' first"
    exit 1
fi

if [ -z "$ZIMAGE_FILE" ] || [ ! -f "$ZIMAGE_FILE" ]; then
    echo "error: $ZIMAGE_FILE not found - copy it from your kernel build output"
    exit 1
fi

echo "DDR init..."
uv run aipc-ddr-init --firmware "$FIRMWARE"

echo "sending coldboot..."
uv run ak7802-usbboot write "$COLDBOOT_FILE" --addr "$COLDBOOT_BASE"

echo "sending linux..."
uv run ak7802-usbboot write "$ZIMAGE_FILE" --addr "$DDR_LINUX_BASE"

uv run ak7802-usbboot exec --addr "$COLDBOOT_BASE"
