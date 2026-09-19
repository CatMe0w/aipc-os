# Unimplemented window probes

These probes measure what a read of the unimplemented L2 addresses returns. The window is 0x48001580 to 0x48001FFF, above the last L2 buffer and inside the 8 KB that L2 decodes. A read there returns a value that changes between reads, and a write has no effect. [The result document](../../../docs/aipc-os-original/unimplemented-address-rng.md) holds every number these probes produced.

This file says what each image measures, how to build and run it, and which rules a new probe must follow.

## Images

The three directories differ in how the image runs, not in what it reads.

### `stub/`, the first capture

```sh
make -C baremetal/probes/rng/stub
```

`probe.bin` runs at 0x32000000 from DDR, under the normal GDB stub. It writes a header at 0x32008000 and four streams of one million words each from 0x32010000. Then it stops on an undefined instruction.

| Stream | Content |
| --- | --- |
| `sweep` | Every word of the window in turn, wrapped around. |
| `fixed1580` | 0x48001580, one million times. |
| `fixed1584` | 0x48001584, one million times. |
| `driven` | 0x48001580, with an all-zero or an all-one word written to a connected buffer before each read. |

`analyze.py` reads the dump. It gives the per-bit change rates and the rates per address across the sweep. It also measures how far `driven` follows the written word, and writes the input files for the NIST SP 800-90B tools.

### `cond/`, one address under several conditions

```sh
make -C baremetal/probes/rng/cond
make -C baremetal/probes/rng/cond ROUNDS=16
```

Each image runs at 0x32000000 under the GDB stub and reads 0x48001580 under a set of conditions. The conditions are interleaved: every round visits every condition once. `ROUNDS` sets the round count of `delay_probe` and `temp_probe` at build time. A short build answers whether a change works, and a long one collects the data. Interrupts stay masked while a stub runs, so the GDB stub services no USB until the stub returns. Keep one invocation in the seconds range and get a long span from several of them.

| Image | Action |
| --- | --- |
| `diag_probe.bin` | Checks the read path and timer 2 in about a millisecond. It writes no system controller register, so it is the image to run first on a board in an unknown state. |
| `delay_probe.bin` | Varies how long the bus floats between reads, over five delays. `delay_rev.bin` is the same image with the delays in reverse order. |
| `load_probe.bin` | Drives CPU traffic into connected L2 buffers between reads. It restores every word it writes. |
| `load2_probe.bin` | The same traffic conditions, all held at one inter-read interval. The image times each condition on the board first, then pads the faster ones. |
| `scan_probe.bin` | Runs the address scan of `anyka-l2rng` read for read. It reports all 672 addresses instead of stopping at the first one that passes. |
| `temp_probe.bin` | Reads four addresses, two of them far from the other two. For a series of short runs taken while the board warms up from cold. |
| `cond_probe.bin` | Superseded. It crossed the delays with the two CPU clock sources. It does not return, and the cause is not known. Keep it out of new work. |

`decode.py` reads a capture from `delay_probe`, `load_probe` or `load2_probe`. The other three write short blocks that GDB can print with `x/`, and each source file gives its own header layout.

### `payload/`, the measurements that must run before USB starts

```sh
make -C baremetal/probes/rng/payload PROBE=restart
make -C baremetal/probes/rng/payload PROBE=survey
```

Each build gives a `BOOT-<probe>.BIN` for 0x33000000. These two measurements cannot run as GDB-loaded images. The host link is USB, and USB traffic on the L2 bus lowers the result by a factor of 2.4. The payload therefore replaces `BOOT.BIN` and samples in `probe_init()`, before MUSB starts. It runs the normal GDB stub body afterwards, so the host can read the result out.

| `PROBE` | Action |
| --- | --- |
| `restart` | Takes the first 1000 samples after power-on and leaves 4096 bytes at 0x32008000. One boot gives one row of an SP 800-90B restart matrix. |
| `survey` | Runs the whole cross-device comparison in one boot: four addresses over several passes, every word of the window, and the driven test. It leaves 3080192 bytes at 0x32008000. A progress word advances after each phase, so a dump from a board that stopped part way still yields the phases that finished. |

`restart-collect.sh` collects a restart series. It cycles port power with `uhubctl`, waits for the stub to enumerate, and reads the row. It keeps the row only if the magic and the sample count check out.

## Run

A `stub/` or `cond/` image loads over the GDB stub, runs, and traps back.

```sh
arm-none-eabi-gdb -nx -batch \
  -ex 'set confirm off' \
  -ex 'set remotetimeout 120' \
  -ex 'target remote /dev/cu.usbmodem00011' \
  -ex 'restore baremetal/probes/rng/cond/delay_probe.bin binary 0x32000000' \
  -ex 'set $pc = 0x32000000' \
  -ex 'continue' \
  -ex 'dump binary memory /tmp/run1.bin 0x32008000 0x320B0000' \
  -ex 'detach'
```

```sh
uv run --with numpy baremetal/probes/rng/cond/decode.py /tmp/run1.bin
```

The dump range differs per image, because the output size follows the round count and the number of conditions. The range above covers `delay_probe` at the default 256 rounds. For any other image, take the range from the result header. It carries the base address and the byte count of the output block.

Copy `BOOT-<probe>.BIN` to the root of the first FAT partition of the SD card, under the name `BOOT.BIN`. Keep the file you replace. Boot from that card directly, not through aipc-boot. That bootloader starts the LCD, and the LCD DMA master puts traffic on the bus.

Both payloads finish before the host can connect, so a failed read costs no power cycle. Read the result block again instead.

```sh
arm-none-eabi-gdb -nx -batch \
  -ex 'target remote /dev/cu.usbmodem00011' \
  -ex 'dump binary memory /tmp/row.bin 0x32008000 0x32009000'
```

For a restart series, set the hub and the port and let the script run under tmux. Every row is its own file, and the script skips a row that already exists. A kill and a restart therefore cost no repeated power cycle.

```sh
tmux new -s restart -d 'ROWS=1000 HUB=1-3 PORT=3 ./restart-collect.sh'
```

## Rules for a new probe here

### Interleave the conditions

The source drifts on the same time scale as the effects these probes look for. Two conditions that run one after the other therefore mix that drift into the difference between them. Every round must visit every condition once. A first version of the delay probe ran the conditions in a fixed order, and the apparent effect reversed sign when the order reversed.

### Hold the inter-read interval equal across conditions

The interval alone moves the bias. A condition that runs faster than the others carries that difference into its result. `load2_probe.c` shows the pattern: time each condition on the board first, then pad the faster ones.

### Leave `CLKDIV1` alone

That register reads back in a different layout than it is written, so a read-modify-write clears the divider fields. Two runs that did it hung the board. A probe that needs the CPU clock belongs on the cpufreq images, which switch the clock from the I-cache.

### Keep off the L2 buffers the stub uses

Buffers 0 and 1 are the USB staging areas that the GDB stub depends on, and buffers 8 and 9 hold the UART ports. A probe that drives L2 traffic must avoid those four, and must restore every other word it writes.

### Write no unbounded loop

GDB cannot interrupt a stub that is running. A loop that waits for a condition that never arrives costs a power cycle, and the board gives no clue about where it stopped. Bound every wait with a count.

### Raise the GDB remote timeout

At the lower CPU clock the stub answers more slowly than the default allows. GDB then gives up while the board is healthy. A board that looks hung is often a board that needs `set remotetimeout`.

### Sample before MUSB starts when the number has to be an idle-bus number

USB traffic on the L2 bus lowers the result by a factor of 2.4. A sample taken in `probe_init()` also reaches DDR before the host connects, so a problem on the GDB side costs no further boots.

### Do not sample from L2

The payload worker in [`probes/cpufreq`](../cpufreq/README.md) runs its own code and stack from L2 SRAM. A sample taken there carries the bus load it is supposed to measure. Change the clock in the worker, return to DDR, and sample from there.

### Write the result magic last, and clear it before a power cycle

A hub can report per-port power switching, disable the port, and leave the board running. The board then returns the result of its previous boot, and nothing in that result says so. The host must clear the magic before it cuts power, and the probe must write it back. Without this rule a restart series collects one thousand copies of a single boot, and the column analysis reports a source that is completely predictable.
