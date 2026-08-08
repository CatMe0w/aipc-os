# gdbstub

A debug agent loaded from SD by [openNBOOT](../../boot/coldboot/opennboot/), designed to be a replacement for the bootrom USB boot mode.

## Building

```
make
```

Produces `BOOT.BIN`. Copy it to the root of the first FAT partition of the SD card.

`make BASE=0x33A00000` produces a relocated image `gdbstub-0x33A00000.bin` that a stub already running at `0x33000000` can load over USB and jump to, avoiding a card swap while iterating.

## Attaching GDB

macOS + GDB: `arm-none-eabi-gdb -ex 'target remote /dev/cu.usbmodem*'`

macOS + LLDB: `lldb -o "process connect serial:///dev/cu.usbmodem*"`

Debian: `gdb-multiarch -ex 'target remote /dev/ttyACM*'`

On Linux, this udev rule is required:

```
sudo cp 99-aipc-gdbstub.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Features and limitations

`break`, `stepi` and `continue` work. Single-step covers ARM state only and a Thumb pc will be reported as an error. An undefined instruction, prefetch abort or data abort stops the target for inspection.

`monitor md32 0x<addr> [<words>]` reads memory a word (4 bytes) at a time. The address is hex with the `0x` prefix. The count is decimal.

## Handing off to a new image

A stub can load the next one and jump to it without the SD card. Build with `BASE` set to a free DDR address, write the flat image with `load`, then send `c<base>`. Note that the host sees no disconnect across the jump and the new image inherits the USB session.

## Reading the trace

The stub logs to DDR and prints it back through the debugger:

```
(gdb) monitor trace
(gdb) monitor oldtrace
```

`oldtrace` is the log of the run before this one, preserved across a reboot.

The same two windows can be read from bootrom USB boot mode when the stub cannot run:

```
uv run tools/gdbstub/tools/dump_trace.py --opennboot
```

`--opennboot` also prints openNBOOT's log.
