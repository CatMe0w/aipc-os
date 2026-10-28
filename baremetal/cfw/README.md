# CFW

CFW is the custom firmware for stock WinCE.

Each time [aipc-boot](../aipc-boot/) starts WinCE, CFW patches WinCE in DDR before it runs.

Formerly known as nkpf. The name nkpf stands for *NK patchfinder*, after the kernel patchfinder (KPF) of [checkra1n](https://checkra.in). The pattern finder and the console come from [pongoOS](https://github.com/checkra1n/pongoOS).

## Components

Each change that CFW makes is a component. Use the "WinCE CFW" page in aipc-boot menu to turn each component on or off.

| Component | Type | Default | Patches | Function |
| --- | --- | --- | --- | --- |
| `power_hold` | Fix | on | EBOOT | Keeps the device on during boot |
| `battery_check_skip` | Fix | on | EBOOT | Stops the power-off at boot after a false low battery reading |
| `rtc_stall_fix` | Fix | on | NK | Prevents the hang after `BackLight Initializing... OK` on the v1.58.2 device |
| `diag` | Extra | off | NK | Shows interrupts and OAL IOCTL calls on the screen while WinCE runs |

### diag

`diag` installs a small monitor program, the [nk resident](residents/nk/), and hooks the NK interrupt handler and OEMIoControl, the OAL function that receives HAL IOCTL calls.

Every 512 interrupts, the nk resident draws a panel in the top-right corner:

| Row | Values |
| --- | --- |
| `irq` | Interrupt count, and the PC at which the last interrupt occurred |
| `sysintr` | The SYSINTR values (logical interrupt numbers) seen since boot. Values seen since the last update are white, the others gray |
| `ioctl` | OEMIoControl calls since boot, and how many of them set the SPI chip select of the CH374 USB driver |
| next rows | The last 6 other IOCTL calls, newest first: the slot number of the calling process, the IOCTL code, and the first two words of the input buffer |

In the IOCTL rows, `-` marks a word past the end of the input buffer, and `?` marks a value the nk resident cannot safely read.

## Errors

When the pattern of an enabled component or of the handoff does not match, CFW stops the boot. It does not skip the patch. Windows CE (unmodified) in aipc-boot still boots with no patch.

- In EBOOT, aipc-boot shows `CFW could not patch EBOOT` with the reason, for example `Missing patch: handoff to nkpf`, and returns to its menu.
- In NK, nkpf shows `panic:` and the reason in red, then halts.

## Log

CFW logs both stages to the 64 KB window at `0x30180000`. The window survives the start of WinCE:

```
(gdb) monitor md32 0x30180000 256
```

## Building

From `baremetal/`, `make` builds CFW and then aipc-boot, which carries CFW. `make -C cfw` builds only CFW, in `build/`.

## For developers

Each fix component has a description of the fault that it fixes:

- `power_hold`: [boot-flow.md](../../docs/eboot/boot-flow.md#power_on_reason_init).
- `battery_check_skip`: [battery-driver.md](../../docs/nk/battery-driver.md#low-battery-actions).
- `rtc_stall_fix`: [rtc-poll-stall.md](../../docs/nk/rtc-poll-stall.md).

To add a component, define a `cfw_component_t` in a new file in `components/`, declare it in `cfw.h`, add it to `cfw_components[]` in `core/header.c`, and add the file to `SRCS` in the `Makefile`.

Code that must stay in memory while WinCE runs goes into the nk resident in `residents/nk/`. CFW reaches the symbols that `EXPORTS` lists in `residents/nk/Makefile`.
