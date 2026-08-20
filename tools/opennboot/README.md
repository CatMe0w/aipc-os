# opennboot

Installs [openNBOOT](../../baremetal/opennboot/) into NAND block 0 over USB boot mode.

The firmware itself is in [baremetal/opennboot](../../baremetal/opennboot/). This directory is the host side only.

## Before you start

From a checkout of this repository, build the four binaries the tool uses. This needs `arm-none-eabi-gcc`.

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

4. Read the path it prints for `opennboot.bin`, and read the name of the backup file. Keep that file. It holds the only copy of the bootloader your device shipped with.
5. Type `yes` at the prompt. Anything else stops the install before it erases anything.
6. Wait for `Done`. Do not disconnect during the write.
7. Power cycle the device with `DL_JUMP` or `USB_BOOT` disconnected. openNBOOT now runs in place of the stock bootloader.

The whole run takes a few seconds.

### What install does

1. Waits for the device, then uploads the DDR init stub and runs it. The 1.88 sequence is safe on devices with older firmware.
2. Writes a pattern to `0x30120000` and reads it back. A mismatch means DDR did not come up, and the install stops.
3. Uploads the two NAND stubs from [stub](stub/).
4. Reads all 64 pages of block 0 as raw pages of 2112 bytes.
5. Saves those 135168 bytes to `backup_nboot-<date>.bin` in the current directory. It reads the file back and compares. It refuses to overwrite an existing backup.
6. Builds the new block image. The stock header page stays as it is, except for the page count byte at offset `0x0D`. `opennboot.bin` goes into the pages after the header.
7. Asks for confirmation.
8. Erases block 0 and programs the pages that carry data.
9. Reads block 0 back and compares. The spare bytes stay out of the comparison, because the write does not program them.

Step 6 stops the install if the header page is not one this tool understands: the `ANYKA382` signature must be at offset `0x04`, the declared page size must be 2048, and the installed bootloader must claim no more than 63 pages. A payload larger than 63 pages does not fit in one block and stops the install too.

## Undo an install

```
uv run opennboot restore --image backup_nboot-<date>.bin
```

The device must be in USB boot mode again. `restore` brings DDR up and uploads the same stubs, then it asks for confirmation, erases block 0, writes the file unchanged and verifies it. It reads no backup of its own first, and it rejects a file that is not 135168 bytes long.

The bootrom USB boot mode is in mask ROM, below everything this tool writes. A device that does not start still enters USB boot mode, so `restore` works from any state.

## Other commands

| Command | What it does |
| --- | --- |
| `opennboot run` | Loads openNBOOT into DDR at `0x30000000` and starts it. Writes nothing to NAND |
| `opennboot log` | Reads back a log the last run left in DDR. `--slot` picks the writer, `--all` reads every window |

`run` exercises the whole boot flow without a NAND write. The bootrom does not resume afterwards, so the device needs a power cycle. DDR keeps its contents across that power cycle, which is why `opennboot log` can still read the log back. The window layout is the shared log pool in [baremetal/README.md](../../baremetal/README.md).

## Which images it uploads

The tool uploads four binaries: openNBOOT itself, the two NAND stubs from [stub](stub/), and the DDR init stub from [aipc-ddr-init](../aipc-ddr-init/stub/). `install` and `run` print the path they resolved for openNBOOT, because that is the image that ends up in NAND.

A checkout wins over the copies inside the wheel, so a local `make` takes effect without a reinstall. Outside a checkout, the tool uses the images packed into the wheel by the release workflow.

## If a write fails

`install` reads block 0 and saves it before it erases anything, and it refuses to continue if the saved file does not match what it read. After the write it reads the block back and compares. A failed verify prints the exact page and column that differ, and the command it takes to put the original bootloader back.
