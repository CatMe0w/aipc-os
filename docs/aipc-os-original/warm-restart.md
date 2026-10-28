# Warm Restart

The device has no software restart. This document describes the one we built: a jump back into the bootrom, preceded by a short list of blocks that the caller must stop.

One of them, the USB block, decides whether the restart works at all. Three others, the backlight, the scan out and the speaker amplifier, decide whether the restart looks and sounds like a fault while it runs.

## Why Jump Back

The AK7802 has no full-chip reset register. The high half of `SYSCTRL+0x0C` holds a software reset bit for each module, but no bit resets the part. The `#RST` pin is an input, and no net on this board drives it from the SoC side. The reset path of the original firmware goes through the RTC watchdog, and that path is dead here. `OALIoCtlHalReboot` waits forever on its first indexed RTC read, because this board leaves the RTC crystal unfitted, and the wait has no timeout. See [power-management.md](../nk/power-management.md#rtc-clock-source).

The only restart left is a re-entry of the bootrom.

## Which Entry Point

Enter at `0x0000005C`, the normal-boot path. The reset vector at `0x00000000` also works, but it switches the device off. It runs `detect_boot_override`, which forces GPIO104 and GPIO105 back to inputs to sample them as straps, and GPIO105 is the `POWER_ON` hold. The main 5 V enable therefore drops for the length of the sampling, and the device switches off unless the user holds the power key. See [docs/eboot/boot-flow.md](../eboot/boot-flow.md).

`0x5C` starts after the strap check. It keeps the GPIO direction and output registers, thus `POWER_ON` stays driven. It does not write `SYSCTRL+0x0C`, which the reset vector sets to `0x59DB`, thus the module clock state of the caller survives into the bootrom. All module clocks on is a state that works.

## Restarting Into Another Mode

`0x5C` is one of four fixed entry points. A jump to any of them goes past the strap check straight to the handler of that mode:

| Mode | Entry | Setup the caller owes |
| --- | --- | --- |
| Normal boot | `0x005C` | none |
| USB boot | `0x3110` | `CPSR = 0x13`, `SP = 0x48000FFC`, `SYSCTRL+0x54 = 0x01000000` |
| UART console | `0x00C4` | none |
| Diagnostic | `0x1260` | `CPSR = 0x13`, `SP = 0x48000FFC`, `SYSCTRL+0x54 = 0x05000000` |

The USB boot entry gives the equivalent of a `reboot bootloader`. Every other way into that mode needs the `USB_BOOT` pin held high across a power cycle, because released straps read low. [docs/bootrom/boot-flow.md](../bootrom/boot-flow.md) has the dispatch addresses and the preconditions of each mode.

## What the Jump Needs

1. Turn the USB block off. See [The USB Block](#the-usb-block).
2. Mask the module interrupt sources at `SYSCTRL+0x34` and `SYSCTRL+0x38`. The bootrom does not write these masks, it unmasks IRQ and FIQ at entry, and its IRQ vector forwards through the word at `0xF0` to `0x30000018`. Without the masks, a pending interrupt enters whatever the caller left at that address.
3. Turn the backlight off, then stop the scan out. See [The Screen](#the-screen).
4. Drive the speaker amplifier enable low. See [The Speakers](#the-speakers).

The pin multiplexer needs no repair. The bootrom probes only SPI and NAND, and each probe sets its own pins before it reads.

## The USB Block

The USB block is a bus master into the L2 buffer SRAM, and three of its windows overlap the memory that the bootrom needs. See [memory-map.md](../bootrom/memory-map.md) for the L2 layout.

| L2 window | USB use | Bootrom use |
| --- | --- | --- |
| `0x48000000` | EP2 bulk IN staging | inside buffer 0, the NAND read buffer |
| `0x48000200` | EP3 bulk OUT DMA target | SPI and NAND read target |
| `0x48001500` | EP0 staging | 0x7C bytes below the stack top at `0x4800157C` |

A restart that leaves the block live succeeds less than one time in ten, because any transfer that the host starts lands in one of these windows and corrupts a read buffer or the stack. With the block off, the restart worked on every attempt in our tests.

Clear the low three bits of `SYSCTRL+0x58`, which is how the bootrom itself turns the block off. The module reset bits in the high half of `SYSCTRL+0x0C` are no substitute: a probe that set bit 31 there stopped the boot earlier than a bare jump does.

## The Screen

Neither the bootrom nor the register init script of the boot image writes the backlight PWM or the LCD controller. Both stay as the caller left them until EBOOT programs them again. The USB boot entry waits for the host in a loop with no exit, see [docs/bootrom/usb-boot.md](../bootrom/usb-boot.md), so there they stay indefinitely. A lit panel in this state looks like a hardware failure. With one version of the Linux display driver the picture faded until the LED array of the backlight showed through, and with a later version the panel turned white.

Turn the backlight off with a high time of zero and a full period of low time in the PWM register at `SYSCTRL+0x2C`, wait for the current PWM period to end, then stop the scan out with bit 0 of `+0xB8`. A register value of zero is not a reliable way to turn the backlight off, because after full duty it left the backlight on. With the backlight off, nothing on the panel is visible, whatever its state, and in this order the backlight goes off while the panel still shows a normal frame. Scan out only reads memory, so unlike the USB block it does not threaten the restart. See [LCD controller](../soc/lcd.md) for the PWM encoding and the stop bit.

## The Speakers

When its transfers stop, the DAC holds its output at the last sample. With the amplifiers on, the speakers carry that level and the background noise of the analog section until the next stage programs the audio block, and on the USB boot entry without end. Drive GPIO69, the active-high enable of both amplifiers, low before the jump. See [Audio DAC](../soc/audio.md#on-this-board).

The DAC transfer uses L2 buffer 3, at `0x48000600`, which no bootrom window overlaps. The DAC therefore does not threaten the restart.

## What the Next Stage Inherits

No peripheral receives a reset signal, so each stage after the jump must initialize each block it uses completely. Besides the clock gates and the GPIO registers, see [Which Entry Point](#which-entry-point), two states matter:

- The L2 buffer assignment at `0x2002C090` keeps the bits of every path the caller set up. The bootrom clears and sets only the bits of its own path. The DAC assignment in bits 29:27 and the SD assignment in bits 14:12 stay as Linux left them.
- The SD card has no power control on this board, so it keeps the relative address, the four-bit bus width and the high-speed timing that Linux set. A driver must start with `CMD0`.

WinCE meets this requirement, and it boots and runs after a restart from Linux.
