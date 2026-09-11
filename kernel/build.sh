#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${BUILD_DIR:-$here/build}"
download="$build/download"

# CONFIG_EXTRA_FIRMWARE_DIR is "../firmware", a sibling of the source tree.
firmware="$build/firmware"

# shellcheck source=sources.lock
source "$here/sources.lock"

source_root="$build/linux-$LINUX_VERSION"
blob_dir="$firmware/ath6k/AR6002"
output="$build/zImage"

firmware_only=0
case "${1:-}" in
    "") ;;
    --firmware-only) firmware_only=1 ;;
    *)
        echo "usage: $0 [--firmware-only]" >&2
        exit 2
        ;;
esac

missing=""

need() {
    command -v "$1" >/dev/null || missing="$missing $1"
}

check_sha256() {
    echo "$2  $1" | sha256sum -c --quiet -
}

matches_sha256() {
    echo "$2  $1" | sha256sum -c --status -
}

download_file() {
    local url="$1"
    local dest="$2"

    if [ ! -f "$dest" ]; then
        echo "Downloading $(basename "$dest")"
        curl -fL --retry 3 -C - -o "$dest.part" "$url"
        mv "$dest.part" "$dest"
    fi
}

if [ "$(uname -s)" != Linux ]; then
    echo "The kernel build requires Linux. Run this script in a Linux VM." >&2
    exit 1
fi

need bc
need bison
need curl
need flex
need make
need patch
need sha256sum
need tar
need xz

if [ -n "$missing" ]; then
    echo "Missing tools:$missing" >&2
    echo "On Debian and Ubuntu, install them with:" >&2
    echo "  sudo apt-get install bc bison build-essential curl flex gcc-arm-linux-gnueabi libssl-dev xz-utils" >&2
    exit 1
fi

cross="${CROSS_COMPILE:-}"
if [ -z "$cross" ]; then
    for candidate in arm-linux-gnueabi- arm-none-eabi-; do
        if command -v "${candidate}gcc" >/dev/null; then
            cross="$candidate"
            break
        fi
    done
    [ -n "$cross" ] || {
        echo "No ARM toolchain found." >&2
        echo "Install gcc-arm-linux-gnueabi, or set CROSS_COMPILE." >&2
        exit 1
    }
fi

# blob_needs_staging <file name> <sha256>
blob_needs_staging() {
    [ ! -f "$blob_dir/$1" ] || ! matches_sha256 "$blob_dir/$1" "$2"
}

# install_blob <file name> <sha256>, from $staged
install_blob() {
    cp "$staged/$1" "$blob_dir/$1"
    check_sha256 "$blob_dir/$1" "$2"
}

mkdir -p "$download" "$blob_dir"

if blob_needs_staging athwlan.bin.z77 "$ATHWLAN_SHA256" ||
   blob_needs_staging data.patch.hw2_0.bin "$DATA_PATCH_SHA256" ||
   blob_needs_staging eeprom.bin "$EEPROM_SHA256" ||
   blob_needs_staging eeprom.data "$EEPROM_DATA_SHA256"; then
    firmware_tarball="$download/$(basename "$FIRMWARE_URL")"
    download_file "$FIRMWARE_URL" "$firmware_tarball"

    echo "Staging the AR6002 firmware"
    temporary="$(mktemp -d "$build/.firmware.XXXXXX")"
    trap 'rm -rf "$temporary"' EXIT
    tar -xJf "$firmware_tarball" -C "$temporary" --strip-components=1 \
        --wildcards --no-anchored 'ath6k/AR6002/*'
    staged="$temporary/ath6k/AR6002"

    install_blob athwlan.bin.z77 "$ATHWLAN_SHA256"
    install_blob data.patch.hw2_0.bin "$DATA_PATCH_SHA256"
    install_blob eeprom.bin "$EEPROM_SHA256"
    install_blob eeprom.data "$EEPROM_DATA_SHA256"
fi

if [ "$firmware_only" = 1 ]; then
    echo "Done: $blob_dir"
    exit 0
fi

series="$(cat "$here"/patches/*.patch "$here/sources.lock" | sha256sum | cut -d' ' -f1)"
if [ "$(cat "$source_root/.aipc-series" 2>/dev/null || true)" != "$series" ]; then
    tarball="$download/linux-$LINUX_VERSION.tar.xz"
    download_file "$LINUX_URL" "$tarball"
    check_sha256 "$tarball" "$LINUX_SHA256"

    echo "Unpacking Linux $LINUX_VERSION"
    rm -rf "$source_root"
    tar -xJf "$tarball" -C "$build"
    if [ ! -f "$source_root/Makefile" ]; then
        echo "$(basename "$tarball") has an unexpected layout." >&2
        exit 1
    fi

    echo "Applying patches"
    for p in "$here"/patches/*.patch; do
        echo "  $(basename "$p")"
        patch -p1 -s -d "$source_root" < "$p"
    done
    printf '%s\n' "$series" > "$source_root/.aipc-series"
fi

echo "Building the kernel with ${cross}gcc"
make -C "$source_root" ARCH=arm CROSS_COMPILE="$cross" aipc_defconfig
make -C "$source_root" ARCH=arm CROSS_COMPILE="$cross" -j"$(nproc)" zImage dtbs

cat "$source_root/arch/arm/boot/zImage" \
    "$source_root/arch/arm/boot/dts/anyka/ak7802-netbook.dtb" > "$output"

echo "Done: $output"
