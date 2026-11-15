# Power Management

The WinCE power stack is split between `PowerOff.exe`, `pm.dll`, the kernel power transition code, and the AK7802 OAL. This document records the behavior recovered from the v1.58.2 image, and the matching board-level results.

## Power Manager States

`pm.dll` loads named states from `SYSTEM\CurrentControlSet\Control\Power\State`. Its flag-to-name mapping includes:

| Flag | Canonical name |
| ---: | --- |
| `0x00020000` | `suspend` |
| `0x00040000` | `suspend` |
| `0x00200000` | `suspend` |
| `0x00800000` | `reboot` |

Before an off, suspend or reset transition, Power Manager calls `KernelIoControl(0x010100F4, NULL, 0, NULL, 0, NULL)`. The control code is `IOCTL_HAL_PRESUSPEND`. Power Manager then powers down the filesystems and the devices.

For a state with `POWER_STATE_RESET`, Power Manager calls `KernelIoControl(0x0101003C, NULL, 0, NULL, 0, NULL)`, which is `IOCTL_HAL_REBOOT`. A state named exactly `coldreboot` first calls `SetCleanRebootFlag()`. Otherwise the ordinary `reboot` and `coldreboot` states enter the same OAL handler.

The common tail calls `PowerOffSystem()`. A successful `IOCTL_HAL_REBOOT` does not return to that tail. If the OAL reboot handler blocks, as it does on this board, `PowerOffSystem()` is not a fallback.

## HAL Reboot Handler

The v1.58.2 OAL reboot handler runs this indexed RTC sideband sequence through `SYSCTRL+0x50` and `SYSCTRL+0x54`:

1. Read RTC index 4.
2. Write RTC index 4 as `0x29F8`.
3. Read RTC index 5.
4. Write RTC index 5 as `0x2001`.
5. Wait forever for the reset.

The v1.58.2 EBOOT treats the machine as one with no hardware RTC. It sets the BOOTARGS RTC-present flag to zero and uses `SYSCTRL+0x28` timer 5 for software timekeeping. It does not initialize the indexed RTC sideband before it enters NK.

The first index 4 read of the handler never completes from a cold state. The ready indication clears and does not come back within 10,000,000 polls. Indices 0 through 5 behave the same, and so does a read with the USB controller briefly quiesced. The OAL reboot handler therefore cannot reach either watchdog write. [RTC Clock Source](#rtc-clock-source) gives the reason, which is a hardware fault on this board.

## RTC Clock Source

The RTC module on our v1.58.2 device has no clock, because the 32.768 kHz crystal of its oscillator is corroded and does not oscillate. This crystal is `Y6` on the schematic and `Y2` on the v1.58.2 board. This is why the indexed RTC sideband never becomes ready.

The RTC module takes its clock on `XTAL32KI` (pin 138, net `RTC_X1`) and `XTAL32KO` (pin 139, net `RTC_X2`). The part has no internal oscillator for this module. The schematic shows two clock circuits on these nets, and the board fits only the second:

| Circuit | Parts | Fitted |
| --- | --- | --- |
| Crystal across `RTC_X1` and `RTC_X2` | Crystal `Y3`, 10 pF load capacitors `C19` and `C24` | No. The schematic marks all three `NC`, and the board has no parts at these positions |
| Inverter oscillator, on the WiFi page | Inverter `U7` (SN74AHC1GU04), crystal `Y6`, feedback resistor `R95` 10M, series resistor `R98` 330K, capacitors `C69` 10 pF and `C70` 22 pF | Yes |

`U7` takes power from the net `RTC33` through `R54` 0R. Its output, `LF_XATL`, goes to pin 19 of the WiFi module, and through the divider `R31` 150K and `R32` 100K to `RTC_X1`. `RTC_X2` connects to no fitted part.

Two observations identify `Y6` as the fault. First, `Y6` is visibly corroded. Second, the WinCE boot failed gradually. In earlier months the device usually reached the desktop, and on 9 V DC input the [RTC poll stall](rtc-poll-stall.md) occurred on few boots. Then the success rate decreased, until the stall occurred on almost every boot with any power source. This pattern agrees with a crystal that degrades over time. A board with no oscillator fails on every boot.

The RTC power domain is fitted. `U23`, a KB7533 LDO, takes `BAT-7.4V` through `R139` 100R and gives `RTC_3.3V`. `R7` 30K and `R77` 10K divide that down to `RTC_2.5V`. The schematic marks the rail `No Load, Iin<5uA`, thus it stays up off the battery. The RTC module has power and no clock.

The sideband handshake crosses into the RTC clock domain. A domain with no clock never returns the ready indication.

The watchdog output is absent as well. `RTC_WD` (pin 141) is the only output of the RTC watchdog. The schematic gives it no net, and the name appears nowhere else in the drawing. `WAKEUP` (pin 142) is the same. A working oscillator alone would therefore give a watchdog that drives no board signal.

To restore the RTC, replace `Y6` (`Y2` on the board). To use the watchdog as a reset source, also route `RTC_WD`. No software change replaces the oscillator.

## PowerOff.exe Policy

`PowerOff.exe` recognizes the command-line words `POWEROFF`, `BATLOW`, `SLEEP` and `REBOOT`, encoded internally as 1, 2, 4 and 8. All three of its confirmed power-state calls are:

```c
SetSystemPowerState(NULL, POWER_STATE_OFF, POWER_FORCE);
```

The executable never requests `POWER_STATE_RESET`, not even in its `REBOOT` mode.

Immediately before the interactive shutdown path requests `POWER_STATE_OFF`, it sets a 4-byte value to 1 and calls `GetSystemPowerStatusEx`. If `ACLineStatus` is non-zero, it changes the value to 0. It submits the value through the private control code `0x01012BC0`.

The matching OAL handler accepts only 0 and 1, and it stores the value in the global `0x8061F4DC`. It runs no MMIO operation. The paired control code `0x01012BC4` only reads the global back. This state selects whether the final OAL transition can cut the external power-hold signal. It is not a reset request.

## OEMPowerOff

The only consumer of `0x8061F4DC` is the final OAL power routine of the board. A zero value enters the full suspend path, with peripheral state handling and SDRAM self-refresh. A non-zero value skips that path, calls the board power-control helper with false, and returns if power remains.

The board helper gets the configured power-hold pin and its active level, configures the pin as an output, and writes the inverse of the active level when the caller passes false. v1.58.2 EBOOT independently identifies this setting as pin 105 with active level 1. Pin 105 is `DGPIO3` on the schematic, which the GPIO4 output bit 9 represents, and it connects to the `POWER_ON` net on the schematic. The original battery-powered shutdown path therefore drives GPIO105 low.

Hardware confirms the pin number, the direction polarity, the output bit, the special input-bit mapping and the electrical connection. A high output latch preloaded before the switch to output mode reads back high on the physical input, and a drive low reads back low. USB power can keep the CPU and the debug link alive after `POWER_ON` goes low, thus USB continuity is not evidence that the main 5 V hold signal stayed asserted. See [USB Back-Power](#usb-back-power) for the reason.

## Power Key Sense

The power key is not only a hardware enable. The schematic routes a sense signal from it back into the SoC, thus software can see the key.

The key sits on the panel assembly and reaches the mainboard as `ON/OFF`, pin 14 of `CON30`. `SW2` is an unfitted mainboard footprint for the same function. Pressing the key connects `BAT-7.4V` to `ON/OFF`, which feeds `+5V_EN` through `R121` 10K. `POWER_ON` feeds the same node through `R122` 1K and `D7`. The two sources are a wired-OR, and each has a diode or a resistor that stops it from driving the other.

`Q3`, a 2N3904 NPN, inverts and level-shifts:

| Terminal | Connection |
| --- | --- |
| Base | `+5V_EN` through `D11` and `R125` 47K, with `R127` 10K to ground |
| Collector | `POWER_KEY`, with `R117` 47K to `3V3` |
| Emitter | Ground |

`POWER_KEY` is the second name of the `TDO` net, thus it lands on pin 3, `GPIO3` on the schematic. The JTAG group is pins 0 to 3, and bit 0 of `SYSCTRL + 0x78` muxes it. The pin reads as a GPIO input only when that bit is clear.

The sense is active low, and the base divider is what keeps the two power sources apart. With the key released and only the `POWER_ON` latch holding the rail, `+5V_EN` sits near 3 V, and the divider puts the base near 0.5 V. That is below `Vbe`, thus `Q3` stays off and `POWER_KEY` reads high. With the key pressed, `+5V_EN` rises toward the 7.4 V battery rail, the base passes `Vbe`, `Q3` turns on, and `POWER_KEY` reads low. The signal therefore reports the key alone and does not follow the software power hold. Hardware confirms both levels: the pin reads high with the key released and low with it held.

The sense needs the main supply. The key reaches no pin of its own. It connects `BAT-7.4V` to `ON/OFF`, thus it can only raise `+5V_EN` while that rail has a source. USB back-powers `+5V`, which is downstream of the enable, and it does not feed `BAT-7.4V`. On USB alone the key therefore moves nothing and software sees no change, which hardware confirms. A supply on the DC jack restores the sense, with or without the cable.

## USB Back-Power

Every USB connector takes its VBUS from one net, and that net is the main `+5V` rail.

`J4` is the connector that carries `OTG_DP` and `OTG_DM` to the SoC, thus it is the port that USB boot mode and the GDB stub use. Pin 1 of `J4` is `USB-V0`, which joins the `USB1-5V` net, which connects to `+5V`. Between the connector pin and the rail there is one ESD clamp to ground and nothing else: no resistor, no fuse, no diode and no load switch. `J9` and `J10` take VBUS from the same net.

`+5V` is the output of the main step-down converter, and `+5V_EN` is the enable of that converter. The power key and `POWER_ON` both drive the enable. A host that supplies VBUS therefore injects power **downstream** of the only gate that the key or software can operate.

Three consequences follow:

- The device runs with no press of the power key, because the rail is already up.
- `OEMPowerOff`, and any Linux equivalent, cannot switch the device off. A low on GPIO105 turns off a converter that is not the source.
- The host port supplies the whole machine. The schematic budgets 3000 mA for the system and 1500 mA for the USB ports alone, thus a 500 mA host port may not hold the rail up and the rail sags.

The reverse case is also true. With the device on its own supply, `+5V` drives VBUS out of `J4` and into the host port.

USB reaches `+5V` and no further. `BAT-7.4V` and `SWO`, which sit before the converter, stay dead, thus a USB-only setup runs the machine but leaves the power key inert. See [Power Key Sense](#power-key-sense).

No software works around this. Unplug USB for any test of the power path. A cable with the VBUS conductor lifted is the other option, because it keeps the data link and gives the power control back.

The main converter has two footprints on this board, `U5` for a `DS8272` and `U17` for a `KB7008`. `U5` carries an `NC` mark, thus `U17` is the fitted part. Both footprints take the same `+5V_EN`, and the behavior above does not depend on which one is fitted.

## NK Rebuild Address Caveat

The rebuilt v1.58.2 `nk.exe` places its code bytes at PE RVA `0x1000`, but the absolute OAL handler pointers in the ROM data keep link addresses that are `0x1000` lower. Some kernel-to-OAL branches also depend on ROM runtime call fixups that the rebuilt PE does not restore. Do not use the invalid offline branch targets in the generic kernel power wrapper to identify `OEMPowerOff`.

Three other things identify the power routine above: the unique read of `0x8061F4DC`, its complete suspend control flow, and its calls into the recovered GPIO direction and output handlers.

## Driver Limitations

This board gives software no hardware reset. The one reset source in the part is the RTC watchdog. On this board its oscillator does not run, and its output reaches no net. The WinCE `IOCTL_HAL_REBOOT` sequence depends on the same dead sideband, and `PowerOff.exe REBOOT` requests only the ordinary OFF state. Do not copy either path as a verified reset.

A bootrom re-entry restarts the software without a power cycle, see [warm-restart.md](../aipc-os-original/warm-restart.md). Every path in this document runs on the CPU. None of them works after a lockup that stops the CPU. After such a lockup, the operator must remove every power source: the DC supply, the battery and USB. [USB Back-Power](#usb-back-power) gives the reason USB counts as a source.
