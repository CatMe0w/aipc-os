# opennboot

Installs [openNBOOT](../../baremetal/opennboot/) into NAND block 0 over USB boot mode. It also saves all of NAND to a file and writes such a file back.

The firmware itself is in [baremetal/opennboot](../../baremetal/opennboot/). This directory is the host side only.

The Tools page of [aipc-boot](../../baremetal/aipc-boot/) installs and removes openNBOOT on the device, without a PC host. The backup files of the two are interchangeable.

## Before you start

From a checkout of this repository, build the binaries the tool uses first. This needs `arm-none-eabi-gcc`.

```
make -C baremetal/opennboot
make -C tools/opennboot/stub
make -C tools/aipc-ddr-init/stub
```

Then set up the workspace:

```
uv sync
```

On Linux, install the udev rule from [ak7802-usbboot](../ak7802-usbboot/) for access without root.

## Install

1. Put the device into USB boot mode. Connect `DL_JUMP` or `USB_BOOT` before power-on, see [ak7802-usbboot](../ak7802-usbboot/).
2. Connect the device to the host over USB.
3. Run the installer. It waits until the device appears.

   ```
   uv run opennboot install
   ```

4. Read the path it prints for `opennboot.bin`.
5. Type `yes` at the prompt. Anything else stops the install before it erases anything.
6. Wait for `Installation complete.`. Do not disconnect during the write.
7. Read the name of the backup file that it printed, and keep that file. It holds the only copy of the bootloader that the install replaced.
8. Power cycle the device with `DL_JUMP` or `USB_BOOT` disconnected. openNBOOT now runs in place of the stock bootloader.

The whole run takes a few seconds.

### What install does

1. Waits for the device, then uploads the DDR init stub and runs it. The 1.88 sequence is safe on devices with older firmware.
2. Writes a pattern to `0x31000000` and reads it back. A mismatch means DDR did not come up, and the install stops.
3. Uploads the block 0 stub from [stub](stub/).
4. Reads all 64 pages of block 0 as raw pages of 2112 bytes.
5. Builds the new block image. The header page stays as it is, except for the page count byte at offset `0x0D`. `opennboot.bin` goes into the pages after the header.
6. Asks for confirmation.
7. Saves the 135168 bytes of step 4 to `backup_nboot-<NAND ID>-<checksum>.bin` in the current directory. It reads the file back and compares. `<checksum>` is the xxHash32 of the contents, so the same bootloader always gives the same file name. A file of that name with other contents is a damaged backup, and the tool writes it again.
8. Erases block 0 and programs the pages that carry data.
9. Reads block 0 back and compares. The write does not program the spare bytes, so they must read back as `0xFF`.

Step 5 stops the install if the header page is not one this tool understands: the `ANYKA382` signature must be at offset `0x04`, the declared page size must be 2048, and the installed bootloader must claim no more than 63 pages. A payload larger than 63 pages does not fit in one block and stops the install too.

## Undo an install

```
uv run opennboot restore --image backup_nboot-<NAND ID>-<checksum>.bin
```

The device must be in USB boot mode again. `restore` brings DDR up and uploads the same stub, then it asks for confirmation, erases block 0, writes the file unchanged and verifies it. It reads no backup of its own first. It rejects a file that is not 135168 bytes long, and a file whose header page fails the checks of step 5.

The bootrom USB boot mode is in mask ROM, below everything this tool writes. A device that does not start still enters USB boot mode, so `restore` works from any state.

## Back up NAND

```
uv run opennboot backup-nand
```

The device must be in USB boot mode. `backup-nand` reads every page of NAND and writes nothing to the device. A 512 MB NAND takes about 17 minutes.

It writes one file to the current directory:

```
backup_nand-<NAND ID>-<checksum>.img.lz4
```

The file is the NAND image, compressed with LZ4. The image holds every page as it is in the part, 2112 bytes per page: 2048 bytes and the 64 spare bytes. A 512 MB part gives an image of 553648128 bytes.

`<checksum>` is the xxHash32 of the image. The LZ4 frame holds the same value. Use `lz4 -t` to check the file. The read applies no ECC correction, so two backups of the same NAND can differ in a few bits and then have different checksums.

[aipc-nand-extract](../aipc-nand-extract/) can extract the image.

## Restore NAND

```
uv run opennboot restore-nand backup_nand-<NAND ID>-<checksum>.img.lz4
```

The device must be in USB boot mode. `restore-nand` erases every block of NAND and writes the image to it, the spare bytes included.

The image can be a file from `backup-nand`. `restore-nand` accepts an LZ4 file and a raw image. The image must have 2112 bytes for each page of the NAND.

A restore that stops early leaves NAND partly written. Put the device into USB boot mode and run `restore-nand` again.

## Other commands

| Command | What it does |
| --- | --- |
| `opennboot run` | Loads openNBOOT into DDR at `0x30000000` and starts it. Writes nothing to NAND |
| `opennboot log` | Reads back a log the last run left in DDR. `--slot` picks the writer, `--all` reads every window |

`run` exercises the whole boot flow without a NAND write. The bootrom does not resume afterwards, so the device needs a power cycle. DDR keeps its contents across that power cycle, which is why `opennboot log` can still read the log back. The window layout is the shared log pool in [baremetal/README.md](../../baremetal/README.md).

## Which images it uploads

The tool uploads five binaries: openNBOOT itself, the three NAND stubs from [stub](stub/), and the DDR init stub from [aipc-ddr-init](../aipc-ddr-init/stub/). `install` and `run` print the path they resolved for openNBOOT, because that is the image that ends up in NAND.

A checkout wins over the copies inside the wheel, so a local `make` takes effect without a reinstall. Outside a checkout, the tool uses the images packed into the wheel by the release workflow.

## If a write fails

`install` reads block 0 and saves it before it erases anything, and it refuses to continue if the saved file does not match what it read. After the write it reads the block back and compares. A failed verify prints the exact page and column that differ, and the command it takes to put the original bootloader back.
