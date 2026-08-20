# SD card image

One card covers both boot paths.

A machine with the stock bootloader starts Windows CE and waits for the user to run [HaRET](haret/README.md). The same card in a machine that runs [openNBOOT](../baremetal/opennboot/) starts Linux by itself.

## Layout

| Partition | Type | Size | Contents |
| --- | --- | --- | --- |
| 1 | FAT32, MBR type `0x0c` | 128 MB | `BOOT.BIN`, `zImage`, `gdbstub.bin`, `haret.exe`, `startup.txt` |
| 2 | ext4 | 3 GB | AOSC OS Afterglow, armv4 |

Partition 1 is first in the table because openNBOOT takes the first FAT partition it finds, and because Windows CE mounts it. Partition 2 is the root filesystem that `CONFIG_CMDLINE` names, so the two must agree. The whole card needs 4 GB or more.

[rootfs.lock](rootfs.lock) pins the root filesystem by URL and SHA256.

## Build

```
make -C baremetal/aipc-boot
make -C baremetal/gdbstub
```

Build the kernel and append the device tree as described in [kernel/README.md](../kernel/README.md). Put the result at `sdcard/build/zImage`, or point `ZIMAGE` at it.

```
./sdcard/build.sh
```

The script needs `genimage`, `mtools`, `dosfstools`, `e2fsprogs`, `curl` and `sudo`. It downloads the root filesystem one time into `sdcard/build/download/`, and it checks the file against `rootfs.lock` on every run. The image goes to `sdcard/build/images/aipc-os-sdcard.img`.

## Write the image to a card

This erases the whole card. Replace `/dev/sdX` with the card.

```
sudo dd if=sdcard/build/images/aipc-os-sdcard.img of=/dev/sdX bs=4M conv=fsync status=progress
```
