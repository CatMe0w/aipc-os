# SD card image

One card covers both boot paths.

For a machine with the stock firmware, a user runs `haret.exe` manually from Windows CE to start Linux.

For a machine with [openNBOOT](../baremetal/opennboot/) installed, the same card starts [aipc-boot](../baremetal/aipc-boot/) boot menu on power-on, which can boot Linux, the GDB stub, DOOM, or stock Windows CE.

The default login is `root` with the password `root`.

## Layout

| Partition | Type | Size | Contents |
| --- | --- | --- | --- |
| 1 | FAT32, MBR type `0x0c` | 128 MB | `BOOT.BIN`, `haret.exe`, `startup.txt`, `aipc-boot/`, `linux/zImage`. See [aipc-boot](../baremetal/aipc-boot/) |
| 2 | ext4 | 3 GB | AOSC OS Afterglow, armv5te |

The card needs 4 GB or more.

[rootfs.lock](rootfs.lock) pins the root filesystem by URL and SHA256.

## Build

1. Build the required baremetal components. From the repository root:

   ```
   make -C baremetal aipc-boot gdbstub doom
   ```
2. Build the kernel and append the device tree as described in [kernel/README.md](../kernel/README.md). Put the result at `sdcard/build/zImage`, or point env `ZIMAGE` at it.
3. Run the build script.

   ```
   ./sdcard/build.sh
   ```

The script writes two files to `sdcard/build/images/`:

- `aipc-os-sdcard.img` is the full card with the default configuration.
- `aipc-os-update.zip` contains partition 1 without `aipc-boot/entries.ini`. See [Update a card](#update-a-card).

## Write the image to a card

This erases the whole card. Replace `/dev/sdX` with the card.

```
sudo dd if=sdcard/build/images/aipc-os-sdcard.img of=/dev/sdX bs=4M conv=fsync status=progress
```

## Update a card

Extract `aipc-os-update.zip` to the root of the FAT32 partition, and overwrite the files that are there. Your boot configs and settings will be intact.
