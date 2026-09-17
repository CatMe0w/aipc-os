# CPU and memory clock probes

These probes answer two questions. Can the CPU clock move without the ASIC clock, and how high can the memory clock run?

`CLKDIV1` is at `SYSCTRL+0x04`, that is physical `0x08000004`. Bit 15 selects PLL1 or the ASIC clock as the CPU clock, and it is the only bit that moves the CPU alone. Every other field in the register also moves the ASIC clock, which carries DDR, the MMC controller, the UARTs, NAND and SPI.

The register model of the memory controller is in [docs/soc/memory-controller.md](../../../docs/soc/memory-controller.md). This file holds the measurements.

## Images

`stub/` builds three images that GDB loads into RAM.

```sh
make -C baremetal/probes/cpufreq/stub
```

| Image | Action |
| --- | --- |
| `cpusrc-measure.bin` | Measures the core clock at both bit 15 settings, checks DDR at each, then restores bit 15. Runs for about 0.4 s. |
| `cpusrc-low.bin` | Moves the CPU to 124 MHz and stops, without restoring bit 15. |
| `cpusrc-high.bin` | Moves the CPU back to 248 MHz and stops. |

Each image runs at `0x32000000`, writes its result to `0x32008000`, and stops on an undefined instruction. The GDB stub then takes over again. The DDR check uses `0x32100000`. Keep that address free.

`cpusrc-measure.bin` turns on the MMU and the I-cache and leaves the D-cache off. The I-cache is what makes the measurement work. The inner loop is two instructions, so it stays in the I-cache and core cycles alone pace it. With the I-cache off, every instruction fetch is a bus access on the ASIC clock. The loop then runs at the ASIC rate, and a change of the core clock alone does not move the result.

The time base is SYSCTRL timer 2, which runs from the 12 MHz crystal and does not follow PLL1. See [docs/soc/timer.md](../../../docs/soc/timer.md).

`payload/` builds two boot payloads. Each one replaces `BOOT.BIN`, runs before USB and the LCD start, and then hands over to the normal GDB stub.

```sh
make -C baremetal/probes/cpufreq/payload PROBE=dqs
make -C baremetal/probes/cpufreq/payload PROBE=asic3x ASIC3X_CONTROL=1
```

| `PROBE` | Action |
| --- | --- |
| `dqs` | Holds one memory clock and tests all sixteen DQS delay values. Stage 8 applies the tuned operating point. Stage 9 reverts. |
| `asic3x` | Tests five ways of arming a /3 ASIC divider. `ASIC3X_CONTROL=1` adds the control point, which is the check that the instrument works. |

These two cannot run as GDB loaded images. The ASIC clock carries DDR, the LCD DMA master and the L2 SRAM path that the USB debug link itself uses. A clock change over a live link therefore blanks the display and stops MUSB from answering, and the result is lost with it. A boot payload instead runs on a bus that openNBOOT leaves idle. It starts MUSB afterwards, so the MUSB initialization clears whatever state the clock change left behind.

The worker runs from L2 SRAM, because memory is unreachable while DDR is in self refresh. `make` checks that `.l2text` calls and literals stay inside the copied section. The build fails otherwise.

## Run

Change `/dev/cu.usbmodem00011` to your own device node.

```sh
arm-none-eabi-gdb -batch \
  -ex 'target remote /dev/cu.usbmodem00011' \
  -ex 'restore baremetal/probes/cpufreq/stub/cpusrc-measure.bin binary 0x32000000' \
  -ex 'set $pc = 0x32000000' \
  -ex 'continue' \
  -ex 'dump binary memory /tmp/cpusrc.bin 0x32008000 0x32008100'
```

```sh
uv run baremetal/probes/cpufreq/decode.py /tmp/cpusrc.bin
```

Run `cpusrc-low.bin` and `cpusrc-high.bin` the same way, without the dump. USB runs from the 60 MHz clock and DDR from the ASIC clock, thus neither one moves when the CPU does. The direct test of the low setting is whether the debug session still works after `cpusrc-low.bin`.

For a payload, copy `payload/BOOT.BIN` to the root of the first FAT partition of the SD card, in place of the usual one, and keep the file you replace. Boot from that card, not through aipc-boot, because aipc-boot starts the LCD.

The `asic3x` payload runs at boot. Read its result block afterwards.

```sh
arm-none-eabi-gdb -batch \
  -ex 'target remote /dev/cu.usbmodem00011' \
  -ex 'dump binary memory /tmp/asic3x.bin 0x32008000 0x32008120'
```

The `dqs` payload runs nothing at boot. It comes up as a plain GDB stub and runs one stage when the debugger asks for it. A stage that stops the board therefore costs nothing but that stage. Power cycle the board and it boots back into a working stub.

| Stage | Memory clock | PLL1 |
| --- | --- | --- |
| 0 | 124 MHz, the boot clock | 248 |
| 1 | 150 MHz | 300 |
| 2 | 160 MHz | 320 |
| 3 | 162 MHz | 324 |
| 4 | 164 MHz | 328 |
| 5 | 166 MHz | 332 |
| 6 | 168 MHz | 336 |
| 7 | 170 MHz | 340 |
| 8 | applies the tuned operating point and does not revert | 320 |
| 9 | reverts to the boot values | 248 |

Stages 0 to 7 sweep all sixteen DQS values at one clock. Each one restores the entry clock and timings afterwards. Write the stage number to `0x32007F00`, then enter at `probe_trigger`.

```gdb
set *(unsigned int *)0x32007F00 = 0
set $pc = 0x33002a00
continue
dump binary memory /tmp/dqs.bin 0x32008000 0x32008160
```

`0x33002a00` is `probe_trigger`. Check it against the build with `arm-none-eabi-nm dqs.elf | grep probe_trigger`, because it moves whenever the image changes.

In a `-batch` session a command that follows `continue` can fail with `Cannot execute this command while the target is running`. The probe still runs. Put `continue` last, then read the result block in a second GDB invocation.

Nothing here writes NAND. A power cycle restores the board, and openNBOOT reads `BOOT.BIN` from SD again.

## Result

Measured on the v1.58.2 device.

### Bit 15 halves the core clock and moves nothing else

At entry `CLKDIV1` held `0x00008000`, thus PLL1 and the CPU were at 248 MHz and the ASIC clock at 124 MHz.

| Point | Batches | Ticks | Implied MHz | DDR errors |
| --- | --- | --- | --- | --- |
| baseline, CPU on PLL1 | 1231 | 1200228 | 248.0 | 0 |
| switched, CPU on ASIC | 618 | 1201381 | 124.4 | 0 |
| restored, CPU on PLL1 | 1231 | 1200225 | 248.0 | 0 |

The ratio is 1.9938, that is 0.3 percent below 2.00. The shortfall is the one timer read in each batch. That read is a bus access on the ASIC clock and does not scale with the core.

The baseline gives 4.03 cycles for each iteration of the two instruction loop. A `subs` plus a taken `bne` costs 4 cycles on an ARM926EJ-S, thus the loop ran from the I-cache and core cycles paced it.

The switch needs no L2 SRAM trampoline. This probe ran from DDR across both switches. A PLL or ASIC divider change does need one.

The `PLL1_EN` strobe cleared before the first poll read at both switches, with a poll count of 0. Keep the poll bound anyway, because nothing promises that it always clears this fast. `CLKDIV1` read back as `0x00000000` after the switch to the ASIC clock, and `0x00008000` after the switch back.

`cpusrc-low.bin` left the board at 124 MHz and the GDB session stayed usable. A memory write and read back, a register read, a `stepi` that reported `SIGILL` on the stop instruction, and a 64 KB dump all worked. The board can therefore run at either bit 15 setting, and neither one needs a change to any peripheral clock.

### There is no /3 ASIC divider

PLL1 cannot rise while the ASIC clock stays where it is. No bit on this part divides the ASIC clock by three.

`Include/platform/AK7802/anyka_cpu_780x.h` in the [Intrisit8000](https://github.com/DanielGit/Intrisit8000) BSP documents `SYSCTRL+0x64` bit 28 as `asic = pll1_clock /3`. The AK98 driver puts its own 3X bit at `SYSCTRL+0x04` bit 28 and commits it with `PLL_CHANGE_ENA`, bit 12, where the header implies `ASIC_AP_EN`, bit 14. The probe crosses both registers with both strobes, which gives four variants, and adds a fifth that sets everything.

Every measurement runs with the CPU on the ASIC clock, so the ALU loop counts ASIC cycles. Before the probe tries any variant, it moves the ASIC divider from /2 to /4 and measures again. That is the control point, and it must read as a factor of two. If it does not, the instrument is broken and no number after it is evidence. The control point read 1.9957 in this run and 1.9966 in an earlier one. The ALU loop therefore measures the ASIC clock to better than 0.2 percent.

| Variant | ALU ratio | Register ratio | `CLKDIV1` bit 28 after | `ANALOG_CTRL2` bit 28 after |
| --- | --- | --- | --- | --- |
| `0x64` bit 28, strobe 14 | 1.0026 | 1.0000 | 0 | 1 |
| `0x64` bit 28, strobe 12 | 1.0026 | 1.0000 | 0 | 1 |
| `0x04` bit 28, strobe 14 | 1.0026 | 1.0000 | 0 | 0 |
| `0x04` bit 28, strobe 12 | 1.0026 | 1.0000 | 0 | 0 |
| both bits, both strobes | 1.0026 | 1.0000 | 0 | 1 |

A working /3 would have read 1750 ALU ticks against the reference 1167. Every variant read 1170, and that 0.26 percent is one tick of quantization.

`CLKDIV1` bit 28 does not hold a written 1, thus the AK98 placement does not exist here. `ANALOG_CTRL2` bit 28 does hold a written 1, but it drives no clock, thus the AK7802 die does not implement what that header describes.

#### The register loop pays a fixed cost for each read

The second instrument is a loop of SYSCTRL register reads. It reads 1.4422 at the control point, not 2.00. Solve `t = scaled + fixed` across the reference and the control point: of the 1807 reference ticks, 799 follow the ASIC clock and 1008 do not. The fixed part is 5.0 crystal ticks for each read. A SYSCTRL read crosses into the crystal clock domain and pays a fixed synchronizer cost.

That is a property of the instrument, not a failed measurement. The loop still agrees with the result above, because a working /3 would have moved its ratio to about 1.221. The fixed part came out as 1008 ticks in both runs.

### Self refresh holds DDR across a clock change

The board came back from an ASIC divider change of /2 to /4 and back, with memory intact. Both points measured after the clock went back read 1.0009 of the reference, that is 0.09 percent. Without the self refresh pair the same change stops the board. The sequence is in [docs/soc/memory-controller.md](../../../docs/soc/memory-controller.md).

### The memory ceiling is 160 MHz, and DQS delay sets it

The DQS delay field behaves like a delay line. At 124 MHz values 0 to 7 all pass and 8 to 15 all fail completely. The clean window narrows as the clock rises, and the upper edge of that window is what stops the board.

| Memory clock | Clean DQS values | Width | Boot value 5 |
| --- | --- | --- | --- |
| 124 MHz | 0 to 7 | 8 | inside |
| 150 MHz | 0 to 5 | 6 | at the edge |
| 160 MHz | 0 to 4 | 5 | one step outside, 4 to 9 errors |
| 170 MHz | none | 0 | 1024 errors |

The board boots at DQS 5. At 160 MHz that value sits one step outside the window, and that one step is the whole of the failure: 4 to 9 bad words out of 1024, and the count varies between runs. DQS 0 to 4 give no errors at the same clock with the same timing word.

At 170 MHz the shape changes. No value is clean. The values that were clean at 160 MHz now give 2 to 7 errors each, instead of a clean pass or a total failure. An error rate that does not follow the sampling phase comes from a different limit.

The clock delay field, bits 23 to 20, boots at 0. At 170 MHz the best DQS value gives 2 to 7 errors with a clock delay of 0. Raising the clock delay to 1 takes that count to about 290, thus 0 is the correct value for this board. To reproduce this, raise the matching `stage_clkd` entry.

The steps between 160 and 170 MHz do not repeat. 164 MHz passed three runs, then gave 2 errors on a later boot, then passed twice more. On one boot 170 MHz passed with no errors, then gave 96 and 108 errors on the two runs straight after it. A higher clock that reads better than a lower one on the same boot is not a property of the memory. 160 MHz is the highest clock that has never failed. Every result above it is inside the spread of the measurement.

The error count at DQS 5 does climb in order across that range: 0, 8, 356, 770, 1024. That is the window edge moving past DQS 5, and it is the one result above 160 MHz that holds on every boot.

### A tuned operating point

Stage 8 applies the point and returns without putting it back, thus the debug session itself becomes the test. Stage 9 reverts.

| Register | Boot | Applied |
| --- | --- | --- |
| `CLKDIV1` | `0x00008000` | `0x00008012` |
| `SDRAM_CFG2` | `0x0F506B95` | `0x0FE88FDD` |
| `SDRAM_CFG3` | `0x00057C58`, DQS 5 | `0x00027C58`, DQS 2 |

That is PLL1 and the core at 320 MHz and the memory at 160 MHz, both 29 percent above the boot values. The core needs no separate measurement. The memory clock is PLL1 divided by two and bit 15 puts the core on PLL1, thus a bus that measures 159.9 MHz is a 320 MHz core.

A deterministic integer workload gives `0xE7C49DEB` at the boot clock and the same value at 320 MHz across three runs. The 4 KB pattern test gives no errors in both passes on every run. A 256 KB dump compared against the flashed `BOOT.BIN` differs in 59 bytes, all inside `.data`, thus 14 KB of `.text` and `.l2text` is byte identical. Stage 9 put every register back, and the workload still gave `0xE7C49DEB`.

Both changes are needed. Neither the DQS delay nor the recomputed timings alone reaches 160 MHz.

## Not covered

This is one board, at one temperature, with a 4 KB pattern and a workload of a few milliseconds. It places the operating point, and it is not a soak test. A settled answer between 160 and 170 MHz needs minutes of continuous testing at a known die temperature. The pass criterion must cover the whole run, not one burst.

The peripherals on the ASIC clock all move with it, and none of them were retuned: the MMC divider, the UART baud rate, NAND and SPI. Audio was not touched. Raising PLL1 breaks audio, because the DAC divides PLL1 and 320 is not a whole multiple of what the divisor search needs.
