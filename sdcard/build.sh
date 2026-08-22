#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"

build="${BUILD_DIR:-$here/build}"

# SHA-512 crypt hash for the default root password "root".
root_password_hash='$6$aipcos$esT5q1gBxKXbRVnrCZNtvygUG/IW87dsiFH9RQ4ahluEHMPNO6KYjM9b14xzC3fS/fnQTAQyIwpxt6EXSLApX/'

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

set_root_password() {
    local passwd_file="$build/rootfs/etc/passwd"
    local shadow_file="$build/rootfs/etc/shadow"

    if ! sudo awk -F: '
        $1 == "root" { count++; valid += ($3 == "0" && $4 == "0") }
        END { exit !(count == 1 && valid == 1) }
    ' "$passwd_file"; then
        echo "$passwd_file must contain one root account with UID and GID 0." >&2
        exit 1
    fi

    if ! sudo awk -F: '
        $1 == "root" { count++ }
        END { exit !(count == 1) }
    ' "$shadow_file"; then
        echo "$shadow_file must contain one root entry." >&2
        exit 1
    fi

    sudo sed -i "s#^root:[^:]*:#root:${root_password_hash}:#" "$shadow_file"

    if ! sudo awk -F: -v expected="$root_password_hash" '
        $1 == "root" { count++; valid += ($2 == expected) }
        END { exit !(count == 1 && valid == 1) }
    ' "$shadow_file"; then
        echo "Failed to set the root password in $shadow_file." >&2
        exit 1
    fi
}

mkdir -p "$build/download" "$build/input" "$build/images" "$build/tmp"

if [ -n "${HARET_DIST:-}" ]; then
    haret_dist="$HARET_DIST"
else
    haret_dist="$build/haret/dist"
    BUILD_DIR="$build/haret" "$here/haret/build.sh"
fi

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
stage "$haret_dist/haret.exe"               haret.exe \
    "Build it with: ./sdcard/haret/build.sh"
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

echo "Setting the root password"
set_root_password

echo "Building the image"
sudo genimage \
    --config "$here/genimage.cfg" \
    --inputpath "$build/input" \
    --rootpath "$build/rootfs" \
    --outputpath "$build/images" \
    --tmppath "$build/tmp"

sudo chown -R "$(id -u):$(id -g)" "$build/images"
echo "Done: $build/images/aipc-os-sdcard.img"
