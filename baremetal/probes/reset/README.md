# Reset probes

## RTC probes

`stub/rtc_diag.bin` is the default, non-destructive probe. It runs from DRAM at `0x32000000`, issues only the index 4 read used at the start of the WinCE reboot handler, bounds the ready wait at 10000000 polls, records the result at `0x32008000`, restores `RTC_CONF`, and traps back into gdbstub.

Build and run it with:

```sh
make -C baremetal/probes/reset/stub
arm-none-eabi-gdb -ex 'target remote /dev/cu.usbmodem00011'
```

Then in GDB:

```gdb
restore baremetal/probes/reset/stub/rtc_diag.bin binary 0x32000000
set $pc = 0x32000000
continue
monitor md32 0x32008000 16
```

The result words are:

| Word | Value |
| --- | --- |
| 0 | `0x52544344` magic |
| 1 | Running marker, zero after completion |
| 2-6 | Entry `SYSCTRL+0x04`, `+0x0C`, `+0x4C`, `+0x50`, `+0x54` |
| 7-8 | `RTC_CONF` and `INT_STATEN` immediately after the read command |
| 9 | Poll count, `10000000` on timeout |
| 10-13 | Final `INT_STATEN`, `RTC_CONF`, `RTC_DATA`, and low 14-bit data |
| 14 | One if ready completed before the limit, otherwise zero |
| 15 | `RTC_CONF` after restoring its entry value |

`TARGET=rtc_usb_quiet_diag` repeats the read with the SoC USB clock briefly gated, restores the USB interrupt masks and clock state, then traps back into gdbstub. This distinguishes an RTC-sideband prerequisite from contention with the active USB debug transport.

`TARGET=rtc_scan_diag` issues bounded read requests for all six indexed windows. It records five words per index starting at result word 4: command poll count, command status, readback data, recovery poll count after restoring the original `RTC_CONF`, and recovered status.

The exact WinCE watchdog sequence is kept as the explicit destructive target:

```sh
make -C baremetal/probes/reset/stub TARGET=wince_reboot
```

It is intentionally not the default target. It writes RTC indexed registers 4 and 5 and does not return if reset fails.

`TARGET=gpio105_poweroff` is a destructive power-hold-line probe. It records the entry state at `0x32008000`, preloads GPIO105 high before changing it to an output, verifies the corresponding GPIO4 input bit, then drives GPIO105 low. The target may lose power or disconnect and require the device to be switched off and on again. With a USB cable attached it does none of that, because the host feeds the `+5V` rail past the enable that GPIO105 drives. See [docs/nk/power-management.md](../../../docs/nk/power-management.md).

If the target remains connected, the result words are:

| Word | Value |
| --- | --- |
| 0 | `0x50575230` magic |
| 1 | Stage: 1 before driving high, 2 before driving low, 0 after completion |
| 2-4 | Entry GPIO4 direction, output and input registers |
| 5 | Entry POWER_ON input level |
| 6-7 | Output and direction registers after driving high |
| 8-9 | POWER_ON input level immediately and after a delay |
| 10-11 | Output register and POWER_ON input level immediately after driving low |
| 12-15 | Final direction, output, input and POWER_ON input level |

## Warm restart

`stub/warm_restart.bin` restarts the device through the bootrom. It runs from DRAM at `0x32000000`. It masks the module interrupt sources, it turns off the USB block, then it jumps to the bootrom normal-boot entry at `0x5C`.

See [docs/aipc-os-original/warm-restart.md](../../../docs/aipc-os-original/warm-restart.md) for why each step is there. The USB shutdown is the step that decides whether the restart works. The entry is `0x5C` and not the reset vector at `0x0`, because the reset vector releases the `POWER_ON` hold to sample the boot straps.

```sh
make -C baremetal/probes/reset/stub TARGET=warm_restart
arm-none-eabi-gdb -ex 'target remote /dev/cu.usbmodem00011'
```

Then in GDB:

```gdb
restore baremetal/probes/reset/stub/warm_restart.bin binary 0x32000000
set $pc = 0x32000000
continue
```

The device runs the bootrom NAND probe again, then nboot, then the payload on the card. GDB loses the target, because the old USB device goes away with it. Start a new GDB session after the payload loads the stub again.

The probe writes a stage color to the framebuffer before each step. The LCD scans DRAM without CPU help, thus the last color on the panel names the last stage that ran:

| Screen | Meaning |
| --- | --- |
| No change | The probe did not start. |
| Red | The probe started. It stopped at the interrupt masks. |
| Yellow | The masks are set. It stopped at the USB shutdown. |
| Green | The USB block is off. The jump ran, and the bootrom stopped before the DDR init script. |
| Green, then white | The bootrom ran the DDR init script, then it stopped. |
| Boot menu | The restart is complete. |
