# SD card image

One card covers both boot paths.

For a machine with the stock firmware, a user runs `haret.exe` manually from Windows CE to start Linux. For a machine with [openNBOOT](../baremetal/opennboot/) installed, the same card starts [aipc-boot](../baremetal/aipc-boot/) boot menu on power-on, which can boot Linux, the GDB stub, or stock Windows CE.

The default login is `root` with the password `root`.

## Layout

| Partition | Type | Size | Contents |
| --- | --- | --- | --- |
| 1 | FAT32, MBR type `0x0c` | 128 MB | `BOOT.BIN`, `zImage`, `gdbstub.bin`, `haret.exe`, `startup.txt` |
| 2 | ext4 | 3 GB | AOSC OS Afterglow, armv4 |

Partition 1 is first in the table because openNBOOT takes the first FAT partition it finds, and because Windows CE mounts it. Partition 2 is the Linux root filesystem that `CONFIG_CMDLINE` specifies. The card needs 4 GB or more.

[rootfs.lock](rootfs.lock) pins the root filesystem by URL and SHA256.

## Build

1. Build the required baremetal components. From the repository root:

   ```
   make -C baremetal/aipc-boot
   make -C baremetal/gdbstub
   ```
2. Build the kernel and append the device tree as described in [kernel/README.md](../kernel/README.md). Put the result at `sdcard/build/zImage`, or point env `ZIMAGE` at it.
3. Run the build script.

   ```
   ./sdcard/build.sh
   ```

The script needs `genimage`, `mtools`, `dosfstools`, `e2fsprogs`, `curl`, and `sudo`. It also builds HaRET. The HaRET build has more requirements in [haret/README.md](haret/README.md).

The output is `sdcard/build/images/aipc-os-sdcard.img`.

## Write the image to a card

This erases the whole card. Replace `/dev/sdX` with the card.

```
sudo dd if=sdcard/build/images/aipc-os-sdcard.img of=/dev/sdX bs=4M conv=fsync status=progress
```
