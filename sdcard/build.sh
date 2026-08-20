#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"

build="${BUILD_DIR:-$here/build}"

# shellcheck source=rootfs.lock
source "$here/rootfs.lock"
tarball="$build/download/$(basename "$URL")"

PATH="$PATH:/usr/sbin:/sbin"

need() {
    command -v "$1" >/dev/null || { echo "missing tool: $1" >&2; exit 1; }
}
need genimage
need mkfs.vfat
need mcopy
need mkfs.ext4
need curl
need sha256sum

mkdir -p "$build/download" "$build/input" "$build/images" "$build/tmp"

probe="$build/.probe"
sudo rm -rf "${probe:?}"
sudo mkdir -p "$probe"
if ! sudo mknod "$probe/testnode" c 1 3 2>/dev/null; then
    sudo rm -rf "${probe:?}"
    echo "$build cannot hold device nodes." >&2
    echo "Point BUILD_DIR at a native Linux filesystem, for example:" >&2
    echo "  BUILD_DIR=\$HOME/aipc-sdcard $0" >&2
    exit 1
fi
sudo touch "$probe/testowner"
sudo chown 0:0 "$probe/testowner"
owner="$(stat -c '%u:%g' "$probe/testowner")"
sudo rm -rf "${probe:?}"
if [ "$owner" != "0:0" ]; then
    echo "$build does not keep file ownership: root-owned files read back as $owner." >&2
    echo "Point BUILD_DIR at a native Linux filesystem, for example:" >&2
    echo "  BUILD_DIR=\$HOME/aipc-sdcard $0" >&2
    exit 1
fi

# stage <source> <name in the FAT partition> <what to do when it is missing>
stage() {
    if [ ! -f "$1" ]; then
        printf 'missing: %s\n%s\n' "$1" "$3" >&2
        exit 1
    fi
    cp "$1" "$build/input/$2"
}

echo "Staging boot files"
rm -f "$build"/input/*
stage "$root/baremetal/aipc-boot/BOOT.BIN"  BOOT.BIN \
    "Build it with: make -C baremetal/aipc-boot"
stage "$root/baremetal/gdbstub/gdbstub.bin" gdbstub.bin \
    "Build it with: make -C baremetal/gdbstub"
stage "$here/haret/haret.exe"               haret.exe \
    "It is committed. Check out the repository again"
stage "$here/haret/startup.txt"             startup.txt \
    "It is committed. Check out the repository again"
stage "${ZIMAGE:-$build/zImage}"            zImage \
    "Build the kernel and append the device tree, then put the result at
$build/zImage or point ZIMAGE at it. See kernel/README.md"

if [ ! -f "$tarball" ]; then
    echo "Downloading $(basename "$URL")"
    curl -fSL -o "$tarball.part" "$URL"
    mv "$tarball.part" "$tarball"
fi
echo "$SHA256  $tarball" | sha256sum -c -

echo "Unpacking the root filesystem"
sudo rm -rf "${build:?}/rootfs"
sudo mkdir -p "$build/rootfs"
sudo tar -xJf "$tarball" -C "$build/rootfs"

echo "Building the image"
sudo genimage \
    --config "$here/genimage.cfg" \
    --inputpath "$build/input" \
    --rootpath "$build/rootfs" \
    --outputpath "$build/images" \
    --tmppath "$build/tmp"

sudo chown -R "$(id -u):$(id -g)" "$build/images"
echo "Done: $build/images/aipc-os-sdcard.img"
