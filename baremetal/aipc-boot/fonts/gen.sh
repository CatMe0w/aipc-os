#!/bin/sh
# Usage: gen.sh DIR
#
# Requires Node.js and npm.
# DIR holds IBMPlexSans-Text.otf from IBM Plex (https://github.com/IBM/plex,
# OFL.txt), and tabler-icons.ttf from @tabler/icons-webfont 3.x (LICENSE.tabler).
set -e
dir=$(cd "$1" && pwd)
cd "$(dirname "$0")"

# gamma-conv.js loads lv_font_conv as a module.
tools=$(mktemp -d)
trap 'rm -rf "$tools"' EXIT
npm install --silent --prefix "$tools" lv_font_conv@1.5.3
export NODE_PATH="$tools/node_modules"

# The edge gamma, see gamma-conv.js
gamma=0.7

conv() {
    name=$1
    size=$2
    shift 2
    node gamma-conv.js "$gamma" --bpp 4 --format lvgl --size "$size" \
        --lv-include lvgl/lvgl.h --lv-font-name "$name" -o "$name.c" "$@"
    # The font directory is local to this machine. Keep it out of the header.
    sed "s|$dir/||g" "$name.c" > "$name.c.tmp"
    mv "$name.c.tmp" "$name.c"
}

ascii=0x20-0x7E
arrows=0x2190-0x2193

text="$dir/IBMPlexSans-Text.otf"
conv sans_16 16 --font "$text" -r $ascii -r $arrows
conv sans_18 18 --font "$text" -r $ascii -r $arrows
conv sans_22 22 --font "$text" -r $ascii

# The ICON_* code points of ../ui_priv.h. icons_20 has the navigation icons
# only.
conv icons_20 20 --font "$dir/tabler-icons.ttf" \
    -r 0xED46,0xEAFF,0xEB10,0xEB40,0xEAC5,0xEB20,0xEB0D
conv icons_18 18 --font "$dir/tabler-icons.ttf" \
    -r 0xED46,0xEAFF,0xEB10,0xEB40,0xEAC5,0xEB20,0xEB0D,0xF384,0xEA34 \
    -r 0xEA60,0xEA61,0xEA06
