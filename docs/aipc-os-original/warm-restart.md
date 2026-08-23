# Warm Restart

The device has no software restart. This document describes the one we built.

The AK7802 has no full-chip reset register. The high half of `SYSCTRL+0x0C` holds a software reset bit for each module, but no bit resets the part. The `#RST` pin is an input, and no net on this board drives it from the SoC side. The reset path of the original firmware goes through the RTC watchdog, and that path is dead here. `OALIoCtlHalReboot` waits forever on its first indexed RTC read, because this board has no working RTC clock domain, and the wait has no timeout.

What remains is re-entry. A program that already runs on the part jumps back into the bootrom. The bootrom then probes storage again and loads the boot chain from the start. The device does not lose power, thus this is a restart of the software, not of the hardware. Anyka uses the same idea in the `reboot` command of their own USB boot stub.

## Which Entry Point

Enter at `0x0000005C`, the normal-boot path, not at the reset vector at `0x00000000`.

Both entries work, but only one keeps the device on. The reset vector runs `detect_boot_override` to sample the DGPIO[3:2] straps, and that function first writes `SYSCTRL+0x94 |= 0x300` to force GPIO104 and GPIO105 back to inputs. GPIO105 is the `POWER_ON` hold. A restart through the reset vector therefore releases the main 5 V enable for the length of the strap sampling, and the device switches off unless the user holds the power key. See [docs/eboot/boot-flow.md](../eboot/boot-flow.md).

`0x5C` starts after the strap check. It keeps the GPIO direction and output registers, thus `POWER_ON` stays driven. It sets its own `CPSR = 0x13` and `SP = 0x4800157C`. It does not write `SYSCTRL+0x0C`, which the reset vector sets to `0x59DB`, thus the module clock state of the caller survives into the bootrom. All module clocks on is a state that works.

This entry is reliable with the USB block off. The screen flickers twice on the way through. That is cosmetic.

## Restarting Into Another Mode

`0x5C` is one of four fixed entry points, and the caller picks which one. The strap decides only what the bootrom picks when it starts by itself. A jump goes straight past the strap check to the handler:

| Mode | Entry | Setup the caller owes |
| --- | --- | --- |
| Normal boot | `0x005C` | none |
| USB boot | `0x3110` | `CPSR = 0x13`, `SP = 0x48000FFC`, `SYSCTRL+0x54 = 0x01000000` |
| UART console | `0x00C4` | none |
| Diagnostic | `0x1260` | `CPSR = 0x13`, `SP = 0x48000FFC`, `SYSCTRL+0x54 = 0x05000000` |

The USB entry gives the equivalent of a `reboot bootloader`, because the host then talks to the device with [ak7802-usbboot](../../tools/ak7802-usbboot/). Every other way into that mode needs the `USB_BOOT` pin held high across a power cycle. All of the setup and register work of [What the Jump Needs](#what-the-jump-needs) applies to these entries too.

[docs/bootrom/boot-flow.md](../bootrom/boot-flow.md) has the dispatch addresses and the per-target preconditions. No test has exercised any entry other than `0x5C`.

## What the Jump Needs

Turn the USB block off first. This is the one step that decides whether the restart works. See [The USB Block](#the-usb-block) below.

Mask the module interrupt sources at `SYSCTRL+0x34` and `SYSCTRL+0x38`. The bootrom writes `CPSR = 0x13` at entry, which unmasks IRQ and FIQ, and its IRQ vector forwards to `0x30000018`.

Restore no register. The bootrom entry sets its own `CPSR` and `SP`, thus nothing of the caller survives into it.

Turn off any other bus master that writes memory the bootrom uses. The LCD controller only reads, thus it can stay on. Its output goes white part way through the restart, because the DDR init script in the image header runs again.

## The USB Block

The USB block is a bus master into the L2 buffer SRAM, and three of its windows overlap the memory that the bootrom needs:

| L2 window | USB use | Bootrom use |
| --- | --- | --- |
| `0x48000000` | EP2 bulk IN staging | inside buffer 0, the NAND read buffer |
| `0x48000200` | EP3 bulk OUT DMA target | SPI and NAND read target |
| `0x48001500` | EP0 staging | 0x7C bytes below the stack top at `0x4800157C` |

A restart that leaves the block live succeeds less than one time in ten. Any transfer that the host starts lands in one of these windows and corrupts a read buffer or the stack. The result looks random, because the outcome depends on what the host does in the first moments after the jump. With the block off, the restart worked on every attempt in the test.

Clear the low three bits of `SYSCTRL+0x58`. This is the same way the bootrom turns the block off before it sets them to 6 to turn it on.

Do not use the software reset bits in the high half of `SYSCTRL+0x0C`. A probe that set bit 31 there stopped the boot earlier than a bare jump does. The names of those bits come from reverse engineering, and bit 31 is not confirmed to be the USB reset.

This part has no SOFTCONN bit. The pull-up stays, thus the host still sees an attached device after the block goes off, and the host tools show no change.

## Straps

`detect_boot_override` forces GPIO104 and GPIO105 to inputs before it reads them. Software therefore cannot hold a strap value across a restart. A restart that goes through the reset vector always takes the normal boot path on this board, because both pins read low once released. To reach another mode, enter its handler directly. See [Restarting Into Another Mode](#restarting-into-another-mode).

## Bootrom Properties That the Restart Depends On

Each probe sets its own pins, thus the caller does not need to repair the pin multiplexer before the jump. The NAND probe calls a hardware init function first, which clears `SYSCTRL+0x74` bits 4 and 3, sets bit 3, then sets `0x00C70200` in `SYSCTRL+0x78`. The SPI probe sets bit 30 of `SYSCTRL+0x78`, and the UART console sets bit 9.

The bootrom does not write the module interrupt masks at `SYSCTRL+0x34` and `SYSCTRL+0x38`, and it unmasks IRQ and FIQ at entry before it does anything else. The vector at `0x18` reads its target from the word at `0xF0`, which holds `0x30000018`. A restart without the masks is therefore safe only for an image that keeps a usable vector table at that address.

See [boot-flow.md](../bootrom/boot-flow.md) for the decision tree that runs after the jump, and [memory-map.md](../bootrom/memory-map.md) for the L2 layout.

## Probe

[baremetal/probes/reset/README.md](../../baremetal/probes/reset/README.md) has a probe that does all of the above, and a table of the stage colors it writes to the screen.

## The Linux Restart Hook

The `.restart` hook of the machine descriptor calls `soft_restart()` with the address of a small routine in the `.idmap.text` section. `soft_restart()` cleans the caches and turns the MMU off, then it enters that routine. See `arch/arm/mach-anyka/restart.S` in the [kernel patches](../../kernel/).

The routine runs with the MMU off and writes the registers at their physical addresses. The restart path runs with interrupts off, thus it cannot `ioremap` them.

Linux keeps no vector table at `0x30000018`, and the timer source is still on when the restart starts. It therefore needs the interrupt masks.
