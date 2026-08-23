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

The v1.58.2 EBOOT configures the machine as a machine with no hardware RTC. It sets the BOOTARGS RTC-present flag to zero and uses `SYSCTRL+0x28` timer 5 for software timekeeping. It does not initialize the indexed RTC sideband before it enters NK.

The first index 4 read of the handler never completes from a cold state. The ready indication clears and does not come back within 10,000,000 polls. Indices 0 through 5 behave the same, and so does a read with the USB controller briefly quiesced. The OAL reboot handler therefore cannot reach either watchdog write in the observed v1.58.2 hardware state.

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

## Driver Boundary

No whole-chip reboot primitive is confirmed. The WinCE `IOCTL_HAL_REBOOT` sequence depends on an RTC sideband that never becomes ready on the v1.58.2 device board, and `PowerOff.exe REBOOT` requests only the ordinary OFF state. Do not copy either path as a verified reset.

## Unresolved

- Whether anything on this part can perform a true hardware reset. A re-entry into the bootrom restarts the software and is reliable, see [warm-restart.md](../aipc-os-original/warm-restart.md), but no register, pin or external part is known that resets the chip itself.
