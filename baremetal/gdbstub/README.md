# gdbstub

A debug agent that [openNBOOT](../opennboot/) loads from SD. It replaces the bootrom USB boot mode.

## Building

```
make
```

One object set gives two images. Both go in the root of the first FAT partition of the SD card:

| Image | Base | Loaded by |
| --- | --- | --- |
| `BOOT.BIN` | 0x33000000 | [openNBOOT](../opennboot/), which loads SD payloads to that address |
| `gdbstub.bin` | 0x33A00000 | [aipc-boot](../aipc-boot/), which itself runs at 0x33000000 |

Both images can sit on the same card. openNBOOT reads `BOOT.BIN`, so the image under that name is the one that boots, aipc-boot or the stub.

`make gdbstub-0x31000000.bin` relocates the image to any other address. A stub that already runs at `0x33000000` or `0x33A00000` can thus load and start the next one without a card swap.

## Attaching GDB

macOS + GDB: `arm-none-eabi-gdb -ex 'target remote /dev/cu.usbmodem*'`

macOS + LLDB: `lldb -o "process connect serial:///dev/cu.usbmodem*"`

Debian: `gdb-multiarch -ex 'target remote /dev/ttyACM*'`

Linux also needs this udev rule:

```
sudo cp 99-aipc-gdbstub.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Features and limitations

`break`, `stepi` and `continue` work. Single-step covers ARM state only, and a Thumb pc gives an error. An undefined instruction, a prefetch abort or a data abort stops the target for inspection.

`monitor md32 0x<addr> [<words>]` reads memory one word (4 bytes) at a time. The address is hex with the `0x` prefix. The count is decimal, 4 by default, and the stub clamps it to 256 words.

## Handing off to a new image

A stub can load the next one and jump to it without the SD card. Build `gdbstub-0x<addr>.bin` at a free DDR address, write the flat image with `load`, then send `c<base>`. The host sees no disconnect across the jump, and the new image inherits the USB session.

## Reading the trace

The stub logs to DDR and prints the log back through the debugger:

```
(gdb) monitor trace
(gdb) monitor oldtrace
```

`oldtrace` is the log of the run before this one. It survives a reboot.

Bootrom USB boot mode can read the same two windows when the stub cannot run:

```
uv run opennboot log --slot gdbstub
```

That prints the previous run and then the current one.
