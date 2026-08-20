# aipc-boot

The bootloader that [openNBOOT](../opennboot/) hands off to. It lives on the SD card as `BOOT.BIN`. It starts the LCD panel and the internal keyboard, then shows a menu with three choices:

- **Linux** loads `zImage` to `0x30008000` and starts it. The command line and the device tree are inside the image, so nothing goes in registers.
- **GDB stub** loads `gdbstub.bin` and enters it at `0x33A00000`, the address that [gdbstub](../gdbstub/) links this build at.
- **Windows CE** reads the stock EBOOT container out of NAND and hands off to it. This is what openNBOOT does when no card is present.

Up and down move the selection. Enter boots. A failed attempt shows its return code on screen and leaves the menu usable.

This is a demonstration bootloader. It does not read a configuration file, it does not check what it loads, and it knows only these three payloads.

## Building

```
git submodule update --init third_party/lvgl
make
```

The build needs `arm-none-eabi-gcc` with newlib. On macOS the Homebrew build usually has no newlib. Install the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) instead, and the Makefile finds it.

## SD card layout

Everything is in the root directory of the first FAT16 or FAT32 partition:

| File          | Purpose                                           |
| ------------- | ------------------------------------------------- |
| `BOOT.BIN`    | this bootloader, loaded by openNBOOT              |
| `zImage`      | Linux kernel with appended device tree            |
| `gdbstub.bin` | from `baremetal/gdbstub/`, the `0x33A00000` build |

The loader finds a file by its 8.3 short name. Thus `zImage` and `ZIMAGE` both work, but `zImage.bin` does not.

## Memory map

The full convention is in [../README.md](../README.md). What aipc-boot touches:

| Address      | Contents                                           |
| ------------ | -------------------------------------------------- |
| `0x30008000` | Linux zImage, up to `0x33000000`                   |
| `0x30037FD4` | EBOOT container, entry at `0x30038000`             |
| `0x301B0000` | log buffer, 64 KB, one slot of the shared log pool |
| `0x33000000` | this image, then the malloc heap                   |
| `0x33800000` | stack top                                          |
| `0x33A00000` | `gdbstub.bin`, up to the framebuffer               |
| `0x33B00000` | framebuffer, 800x480 RGB565                        |

Both load limits are the next address with an owner. For example, a zImage must not reach this aipc-boot image, and `gdbstub.bin` must not reach the framebuffer. An oversized file fails with `rc=-9` instead of an overwrite of either one.

The GDB stub needs two builds, because its `BOOT.BIN` links at `0x33000000`, where aipc-boot itself runs. `make` in `baremetal/gdbstub/` produces both. The one named `gdbstub.bin` is the one that aipc-boot loads.

## Caches

aipc-boot runs with the MMU, both caches and the write buffer on. That is what makes a load of a multi-megabyte kernel from SD take a moment instead of half a minute. DDR is cacheable and MMIO is not. The framebuffer section is uncached, because the LCD controller does not snoop the D-cache. Scanout DMA also gets AHB priority over the core. Without that priority, sustained cached DDR traffic starves the LCD FIFO.

Every handoff path cleans and invalidates the caches, then turns the MMU off. Linux requires that state, and gdbstub and EBOOT both assume it.

## Log

The log goes to DDR and to the UART. Its window is buffered but uncached, so it is readable after a hang without any cache maintenance:

```
(gdb) monitor md32 0x301B0000 256
```

A zImage covers the window. The Linux path therefore calls `log_detach()` before the load and logs only to the UART from there on. Without that call, the lines logged after the load land inside the kernel image, and the kernel never decompresses.
