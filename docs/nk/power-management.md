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

The board helper gets the configured power-hold pin and its active level, configures the pin as an output, and writes the inverse of the active level when the caller passes false. v1.58.2 EBOOT independently identifies this setting as pin 105 with active level 1. Pin 105 is `DGPIO3`, which the GPIO4 output bit 9 represents, and it connects to the `POWER_ON` net on the schematic. The original battery-powered shutdown path therefore drives GPIO105 low.

Hardware confirms the pin number, the direction polarity, the output bit, the special input-bit mapping and the electrical connection. A high output latch preloaded before the switch to output mode reads back high on the physical input, and a drive low reads back low. USB power can keep the CPU and the debug link alive after `POWER_ON` goes low, thus USB continuity is not evidence that the main 5 V hold signal stayed asserted.

## NK Rebuild Address Caveat

The rebuilt v1.58.2 `nk.exe` places its code bytes at PE RVA `0x1000`, but the absolute OAL handler pointers in the ROM data keep link addresses that are `0x1000` lower. Some kernel-to-OAL branches also depend on ROM runtime call fixups that the rebuilt PE does not restore. Do not use the invalid offline branch targets in the generic kernel power wrapper to identify `OEMPowerOff`.

Three other things identify the power routine above: the unique read of `0x8061F4DC`, its complete suspend control flow, and its calls into the recovered GPIO direction and output handlers.

## Driver Boundary

The confirmed Linux poweroff primitive is the GPIO105 active-high power hold. Keep a high output latch while you select output mode, then drive the line low for the final shutdown. A USB-powered development setup can continue to execute afterward.

No whole-chip reboot primitive is confirmed. The WinCE `IOCTL_HAL_REBOOT` sequence depends on an RTC sideband that never becomes ready on the v1.58.2 device board, and `PowerOff.exe REBOOT` requests only the ordinary OFF state. A Linux reboot implementation must not copy either path as a verified reset.

## Unresolved

- Whether another AK7802 register, an external supervisor action, or a carefully defined boot-chain handoff can give a reliable warm reboot without the absent RTC clock domain.
- Whether the removal of USB power, while GPIO105 stays low, causes the expected complete main-rail shutdown and needs a new external power-on event.
