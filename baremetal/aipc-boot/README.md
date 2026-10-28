# aipc-boot

The bootloader on the SD card, as `BOOT.BIN`. [openNBOOT](../opennboot/) starts it on power-on, and HaRET can start it from Windows CE.

It boots Linux, bare-metal payloads, and Windows CE with or without [CFW](../cfw/).

## Configuration

### `entries.ini`

`aipc-boot/entries.ini` (on the first FAT32 partition of the SD card, same below) lists the Linux entries. Each section is one entry, and the section name is its title in the menu.

```ini
[Linux]
type=linux
kernel=/linux/zImage
description=Afterglow 20260929 armv5te, with Linux 7.2.0
cmdline=console=tty0 root=/dev/mmcblk0p2 rootfstype=ext4 rootwait rw
```

| Key | Required | Value |
| --- | --- | --- |
| `type` | yes | `linux` |
| `kernel` | yes | path of a zImage with an appended device tree, from the root of the card |
| `cmdline` | no | the kernel command line |
| `description` | no | one line under the title |

An entry with an unknown `type`, or without `type` or `kernel`, does not boot.

`cmdline` goes to the kernel in an ATAG, so the kernel needs `CONFIG_ARM_ATAG_DTB_COMPAT`. `aipc_defconfig` sets it.

### `settings.ini`

aipc-boot writes `aipc-boot/settings.ini` each time a setting changes in the menu. The file can also be edited by hand, but aipc-boot does not keep comments. A missing key, or a value that is not valid, gives the default.

| Section | Key | Default | Value |
| --- | --- | --- | --- |
| `[boot]` | `default` | the first item | title of the item that autoboot starts |
| `[boot]` | `timeout` | `15` | autoboot delay in seconds, a positive number |
| `[display]` | `backlight` | `100` | backlight in percent, `10` to `100` |
| `[display]` | `theme` | `light` | `light` or `dark` |
| `[cfw]` | a component ID | the default of the component | `on` or `off` |

## Payloads

A payload is a bare-metal ARM ELF file in `aipc-boot/payloads/`. aipc-boot loads each `PT_LOAD` segment to its physical address, and calls the entry point as an AAPCS function with the MMU and the caches off. It refuses a segment outside DDR, or a segment that covers aipc-boot or a framebuffer in the [memory map](#memory-map).

A payload that returns goes back to the menu. It must return in SVC mode, with the MMU and the caches off, and it must not write to `0x33000000` to `0x33800000`, where aipc-boot keeps its code, data and stack.

## Building

From `baremetal/`:

```
git submodule update --init aipc-boot/third_party/lvgl
make aipc-boot
```

This also builds CFW, because `BOOT.BIN` contains the CFW image. The output is `build/BOOT.BIN`. `AIPC_BOOT_VERSION` sets the version on the Info page, and is `dev` by default.

## Memory map

The DDR convention for all components is in [../README.md](../README.md).

| Address      | Contents                                           |
| ------------ | -------------------------------------------------- |
| `0x30000100` | ATAG list for a Linux entry                        |
| `0x30008000` | Linux zImage, up to `0x33000000`                   |
| `0x30037FD4` | EBOOT container, entry at `0x30038000`             |
| `0x30190000` | CFW image                                          |
| `0x301B0000` | log window, 64 KB                                  |
| `0x33000000` | aipc-boot, including `.bss`, below `0x33700000`    |
| `0x33800000` | stack top                                          |
| `0x33B00000` | framebuffer, 800x480 RGB565                        |
| `0x33C00000` | second framebuffer                                 |

## Log

aipc-boot logs to the UART and to the log window at `0x301B0000`. The window is uncached, so it stays readable after a hang, and DDR keeps it across a restart. To read it, restart the device in one of these two ways:

- Into USB boot mode. Then run this command on the host:

  ```
  uvx opennboot log --slot aipc-boot
  ```

  See [opennboot](../../tools/opennboot/) for details.
- Into the [GDB stub](../gdbstub/), with the stub as `BOOT.BIN` on the card. Then run this command in GDB:

  ```
  (gdb) monitor md32 0x301B0000 256
  ```

In order to read the log, DO NOT start the GDB stub from the Payloads page, because aipc-boot clears the window each time it starts.

Note: before aipc-boot loads a zImage or a payload, it stops writing the window and logs only to the UART.

## Third-party code

- [LVGL](third_party/lvgl/)
- [FatFs](third_party/fatfs/)
- Fonts from IBM Plex ([OFL.txt](fonts/OFL.txt)) and Tabler Icons ([LICENSE.tabler](fonts/LICENSE.tabler))
