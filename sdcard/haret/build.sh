#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
sdcard="$(dirname "$here")"
build="${BUILD_DIR:-$sdcard/build/haret}"
download="$build/download"
source_root="$build/source"
toolchain_root="$build/toolchain"
dist="$build/dist"

# shellcheck source=sources.lock
source "$here/sources.lock"

need() {
    command -v "$1" >/dev/null || {
        echo "missing tool: $1" >&2
        exit 1
    }
}

download_file() {
    local url="$1"
    local sha256="$2"
    local output="$3"

    if [ ! -f "$output" ]; then
        echo "Downloading $(basename "$output")"
        curl -fL --retry 3 -C - -o "$output.part" "$url"
        echo "$sha256  $output.part" | sha256sum -c -
        mv "$output.part" "$output"
    fi

    echo "$sha256  $output" | sha256sum -c -
}

if [ "$(uname -s)" != Linux ]; then
    echo "HaRET requires Linux. Run this script in a Linux VM." >&2
    exit 1
fi

case "${CEGCC_ARCH:-$(uname -m)}" in
    x86_64 | amd64)
        cegcc_arch=x86_64
        cegcc_url="$CEGCC_X86_64_URL"
        cegcc_sha256="$CEGCC_X86_64_SHA256"
        ;;
    aarch64 | arm64)
        cegcc_arch=aarch64
        cegcc_url="$CEGCC_AARCH64_URL"
        cegcc_sha256="$CEGCC_AARCH64_SHA256"
        ;;
    *)
        echo "unsupported host architecture: ${CEGCC_ARCH:-$(uname -m)}" >&2
        exit 1
        ;;
esac

need curl
need file
need make
need patch
need python3
need sha256sum
need tar
need unzip

mkdir -p "$download" "$build"

haret_archive="$download/haret-$HARET_COMMIT.tar.gz"
cegcc_archive="$download/cegcc-$cegcc_arch-$CEGCC_VERSION.zip"

download_file "$HARET_URL" "$HARET_SHA256" "$haret_archive"

echo "Extracting HaRET source"
rm -rf "$source_root"
mkdir -p "$source_root"
tar -xzf "$haret_archive" -C "$source_root"
haret_source="$source_root/haret-$HARET_COMMIT"

if [ ! -f "$haret_source/Makefile" ] || [ ! -f "$haret_source/COPYING" ]; then
    echo "HaRET source archive has an unexpected layout." >&2
    exit 1
fi

echo "Applying patches"
for p in "$here"/patches/v1-*.patch; do
    echo "  $(basename "$p")"
    patch -p1 -s -d "$haret_source" < "$p"
done

download_file "$cegcc_url" "$cegcc_sha256" "$cegcc_archive"

toolchain_stamp="$toolchain_root/.sha256"
if [ ! -x "$toolchain_root/cegcc/bin/arm-mingw32ce-g++" ] || \
   [ "$(cat "$toolchain_stamp" 2>/dev/null || true)" != "$cegcc_sha256" ]; then
    echo "Extracting the $cegcc_arch cegcc toolchain"
    rm -rf "$toolchain_root"
    mkdir -p "$toolchain_root"
    unzip -q "$cegcc_archive" -d "$toolchain_root"
    printf '%s\n' "$cegcc_sha256" > "$toolchain_stamp"
fi
cegcc="$toolchain_root/cegcc"

if [ ! -x "$cegcc/bin/arm-mingw32ce-g++" ] || \
   [ ! -x "$cegcc/bin/arm-mingw32ce-objdump" ]; then
    echo "cegcc archive has an unexpected layout." >&2
    exit 1
fi

rm -rf "$dist"
mkdir -p "$dist"

python_shim="$build/bin"
rm -rf "$python_shim"
mkdir -p "$python_shim"
ln -s "$(command -v python3)" "$python_shim/python"

echo "Building HaRET $HARET_VERSION with the $cegcc_arch cegcc toolchain"
mkdir -p "$haret_source/out"
make_haret() {
    PATH="$python_shim:$PATH" \
    SOURCE_DATE_EPOCH="$HARET_SOURCE_DATE_EPOCH" \
    TZ=UTC \
    make -C "$haret_source" \
        BASE="$cegcc" \
        VERSION="$HARET_VERSION" \
        "$@"
}

make_haret out/haret-debug
if [ ! -f "$haret_source/out/haret-debug" ] && \
   [ -f "$haret_source/out/haret-debug.exe" ]; then
    mv "$haret_source/out/haret-debug.exe" "$haret_source/out/haret-debug"
fi
make_haret out/haret.exe

file_output="$(file -b "$haret_source/out/haret.exe")"
case "$file_output" in
    *PE32*executable*ARM*) ;;
    *)
        echo "unexpected HaRET output: $file_output" >&2
        exit 1
        ;;
esac

cp "$haret_source/out/haret.exe" "$dist/haret.exe"

echo "Done: $dist/haret.exe"
echo "  cegcc: $cegcc_arch $CEGCC_VERSION"
echo "  $file_output"
echo "  SHA256: $(sha256sum "$dist/haret.exe" | cut -d' ' -f1)"
