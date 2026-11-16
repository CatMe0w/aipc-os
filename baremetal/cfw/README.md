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
| `rtc_stall_fix` | Fix | off | NK | Prevents the hang after `BackLight Initializing... OK` on a device whose RTC oscillator does not run. Turn it on only if WinCE stops at that message. |
| `diag` | Extra | off | NK | Shows interrupts and OAL IOCTL calls on the screen while WinCE runs |
| `eboot_menu` | Extra | off | EBOOT | Unhide the stock boot menu of EBOOT |

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

### eboot_menu

The stock EBOOT contains a boot menu, but the menu is not complete: EBOOT skips the countdown, and it implements neither the key input nor the text output of the menu. `eboot_menu` supplies these parts. See [FMD Boot Menu and Default Target](../../docs/eboot/boot-flow.md#fmd-boot-menu-and-default-target) for the appearance of the stock menu.

With `eboot_menu` on, EBOOT shows the boot menu and counts down 15 seconds before it starts WinCE. The keyboard works during the countdown and in each submenu. All other EBOOT messages also appear on the screen.

`eboot_menu` installs a small program, the [eboot resident](residents/eboot/), which reads the keyboard and measures the countdown while EBOOT runs.

Warning: The stock boot menu can erase partitions, write the partition table and the boot configuration, and run a NAND test that erases the whole NAND. Make a NAND backup in aipc-boot before you use them.

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

Code that must stay in memory after a stage ends goes into a resident. There is one resident for each stage: the eboot resident in `residents/eboot/` runs inside EBOOT, and the nk resident in `residents/nk/` runs inside WinCE. To give a component such code:

- Add the source file to the resident of that stage. Do not add a third resident.
- List each symbol that CFW needs in `EXPORTS` in the Makefile of the resident, with the component name as prefix. CFW links against these symbols.
- Call `cfw_resident_install()` in `init` of the component. To write a word in the resident from CFW, use `cfw_resident_ptr()`.
- The two residents use the same memory, one after the other, so a resident stops when its stage ends. If it leaves hardware in use, release that hardware in `nk_enter` of the component. CFW code cannot call the eboot resident there.
