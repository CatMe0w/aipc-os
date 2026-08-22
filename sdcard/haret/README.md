# HaRET

HaRET starts Linux directly from Windows CE. This directory contains the build recipe and boot script.

The build uses HaRET commit `ef3a2d0b0791cd733627ebf3c2d1e5232527764b`. It uses cegcc release `2026-04-14-154823` from [Brain Hackers](https://github.com/brain-hackers/cegcc-build).

## Build

Run the script on Linux:

```
./sdcard/haret/build.sh
```

The script supports x86_64 and aarch64 Linux hosts. It selects the correct cegcc archive from the host architecture.

The script needs `curl`, `file`, `make`, `patch`, `python3`, `sha256sum`, `tar`, and `unzip`. Set `BUILD_DIR` to change the build directory. Set `CEGCC_ARCH` to `x86_64` or `aarch64` to override host detection.

The output is `sdcard/build/haret/dist/haret.exe`.

## Patches

`patches/` holds the changes to the HaRET source. The HaRET source is from 2011 and the toolchain is from 2026, so every patch adapts the source to GCC 9 or to binutils 2.34.

| Patch | Reason | Consequence if not applied |
| --- | --- | --- |
| 0001 | cegcc 9 does not declare the Win32 `min()`. | Build fails immediately. |
| 0002 | GCC 9 dropped `-march=armv5`. | Build fails immediately. |
| 0003 | binutils 2.34 prints negative section offsets, which failed `checkrelocs`. | Build fails immediately. |
| 0004 | A GCC 9 jump table put absolute addresses in the relocated preloader. | Build fails immediately. |
| 0005 | GCC 9 calls the sized `operator delete`, which adds an import of `libstdc++-6.dll`. | The executable fails to run due to a missing DLL. |
| 0006 | The default PE linker script collects `*(.text.*)` before `haret.lds` can group those sections. | The executable runs, but cannot start Linux. |

To change the series, apply it with `git am` and export it again. Run these commands from `sdcard/build/haret/`:

```
mkdir -p work && tar -xzf download/haret-*.tar.gz -C work --strip-components=1
cd work
git init -q . && git add -A && git commit -qm upstream && git tag upstream
git am ../../../haret/patches/v1-*.patch
```

```
rm -f ../../../haret/patches/v1-*.patch
git format-patch -v1 --zero-commit --no-signature -o ../../../haret/patches upstream..HEAD
```

## Versioning

HaRET shows its version at startup and writes it to `haretlog.txt`. `HARET_VERSION` in `sources.lock` sets it. It ends with a serial number, which is the only way to tell two builds apart on the device. We increase the serial when we change `patches/` or the pinned upstream commit. The version does not follow the AIPC OS release, because HaRET does not change with it.

## License

GPLv2. See [COPYING](COPYING).
