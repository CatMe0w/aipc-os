# USB HID Input

The AIPC unit has an internal USB HID keyboard that is the **only** input
device available to EBOOT in normal operation. There is no physical UART
console (the UART pads on this board are damaged on the analyzed unit),
and the two external USB-A ports visible on the chassis are also routed
through the same bridge chip. All three USB ports - the two external
ones and the internal keyboard - come from a single **WCH CH374** USB
host bridge chip connected over **SPI0**, not from the AK7802's own
integrated USB controller.

This document describes:

1. SPI0 register layout (distinct from SPI2)
2. CH374 architecture and the SPI-command protocol used to program it
3. The HID keyboard input path through EBOOT
4. The maintenance-mode password gate at a high level

The maintenance menu's full behavior (which menu items exist, what each
one does, how the body is decompiled) is **not documented here**. The
scratchpad analysis of the maintenance menu code path was incomplete and
partly incorrect, and the decompilation has open questions that would
fit better in a dedicated document once the analysis is redone from the
clean EBOOT image. This document is intentionally scoped to the
**input-path hardware layer** so that a future maintenance-mode
document can build on it without rewriting the CH374 details.

## SPI0 (base 0x20020000)

SPI0 is a separate instance from SPI2 even though both appear at similar
offsets in the SoC MMIO space. The two controllers share the same base
offset layout (`+0x00` control, `+0x04` status, `+0x0C` count, `+0x18`
TX data, `+0x1C` RX data), but **the bit assignments inside the control
register are different**.

### SPI0 Control Register (+0x00)

Bit assignments as used by the CH374 driver layer:

| Bit    | Meaning                                                         |
| ------ | --------------------------------------------------------------- |
| 0      | RX mode select (1 = receive, 0 = transmit)                      |
| 1      | START / busy; write 1 to start a transfer, reads as busy flag   |
| 5      | **CS assert** (1 = drive CS low, 0 = release CS high)           |
| others | Clock divider, programmed once at init via a helper             |

Comparison with SPI2: on SPI2, bit 0 is CS (inverted polarity), bit 1 is
direction, and bit 5 is transfer-enable. SPI0 places CS on bit 5 and
puts the RX/TX direction on bit 0. **Code written for SPI2 cannot be
retargeted to SPI0 by changing only the base address.** This was
verified from the `spi_cs_assert` and `spi_cs_deassert` helpers which
write `|= 0x20` and `&= ~0x20` to the SPI0 control register.

### SPI0 Status Register (+0x04)

| Bit | Meaning             |
| --- | ------------------- |
| 2   | TX FIFO ready       |
| 6   | RX FIFO ready       |
| 8   | Transfer complete   |

The status bit positions happen to be consistent between SPI0 and SPI2.
Only the control-register bits differ.

### Chip Select Routing

The CH374's CS line is driven by the SPI0 controller's internal CS
output pin (bit 5 of SPI0 control), not by an external GPIO asserted
from software. EBOOT's `spi_cs_assert` / `spi_cs_deassert` helpers just
set and clear bit 5 of the SPI0 control register; no GPIO writes are
involved.

## CH374 USB Host Bridge

The CH374 is a WCH QinHeng USB 2.0 full-speed host/device bridge chip.
It exposes a byte-level SPI register interface on one side and a USB
host controller with up to 4 endpoints and host-mode support on the
other. On AIPC, the CH374 is configured as a host and presents three
USB ports to the system:

- Two external USB-A ports visible on the chassis
- One internal port wired to the HID keyboard module

EBOOT's CH374 driver is a thin wrapper around the chip's register
interface. It is only used during the maintenance mode path: normal
boot does not talk to CH374 at all (the boot path uses neither the
external USB ports nor the keyboard). When WinCE NK.bin runs, it
re-initializes CH374 for full USB host operation, and EBOOT's CH374
state is irrelevant.

### SPI Command Protocol

CH374 register accesses use a **3-byte SPI command** in the form:

```
byte 0: register index
byte 1: 0x80         (direction-or-command marker)
byte 2: value
```

A register write sends all three bytes; a read sends the first two and
reads the third back. The helper `ch374_reg_write` wraps this pattern.
The exact semantics of the `0x80` second byte are not fully documented
here; it is the same across all accesses observed in EBOOT.

### Registers Programmed at Init

EBOOT's CH374 initialization writes the following register values
(register numbers are CH374-local, not SoC register offsets):

| Register | Stage 1 | Stage 2 | Notes                         |
| -------- | ------- | ------- | ----------------------------- |
| reg 5    | 64      | 64      | `[partial]`                   |
| reg 6    | 0       | 192     | Two-stage init; purpose TBD   |
| reg 7    | 3       | 3       | `[partial]`                   |
| reg 8    | 0       | 0       | `[partial]`                   |
| reg 9    | 31      | 31      | `[partial]`                   |
| reg 14   | 0       | 0       | `[partial]`                   |
| reg 2    | read-clear-bit-7-write | - | Status/interrupt bit clear |

Decoding these fully requires the CH374 datasheet; they are listed here
so a future investigation has a starting point.

The init function itself is called only when EBOOT enters the
maintenance mode path. The regular boot flow (load NK from NAND, jump)
does not call it, so CH374 remains uninitialized on the happy path.

## HID Keyboard Input Path

Once CH374 is initialized, EBOOT polls the keyboard endpoint for HID
boot-protocol reports (8-byte reports containing a modifier byte,
reserved byte, and up to 6 concurrent usage codes). The polling helper
translates each incoming usage code to an ASCII character and hands it
to the menu-driven UI layer. The character delivery path goes through
a function that stores the most recent key into `.data` at byte
`0x80058489`.

This is the mechanism by which the maintenance-mode prompt receives
user input: characters are not read from a UART but from a software
keyboard scan loop that talks to the CH374 through SPI0.

The USB HID usage codes observed for the maintenance password are:

- `0x1D` = `Z`
- `0x17` = `T`
- `0x0E` = `K`
- `0x28` = Enter

The exact scan-code-to-ASCII translation table used by EBOOT is not
tabulated in this document.

## Maintenance Mode Password

The maintenance menu is gated behind a four-character password:
**`Z T K Enter`** (type the letter Z, then T, then K, then press
Enter). The password has been verified on real hardware.

Activation sequence, in order:

1. Power the device with the HID keyboard connected.
2. During the brief window when EBOOT shows its banner, press **F1**
   to enter the password prompt mode.
3. Type `Z`, `T`, `K`.
4. Press Enter to submit.

If the password is accepted, the maintenance menu is displayed. The
menu's contents and individual actions are **out of scope** for this
documentation.

### What is not documented here

- The full set of maintenance-mode menu items, their display strings,
  and their actions are documented separately in
  [maintenance-mode.md](maintenance-mode.md).

## Unresolved

- SPI0 clock divider layout (offsets above bit 5): not documented.
- SPI0 `+0x20` (if such a register exists by analogy to SPI2) is not
  referenced by any path in EBOOT that this document covers.
- Full CH374 register map decode for the seven init registers above.
- CH374 interrupt path: the chip has an INT output pin that is wired
  to an AK7802 GPIO, but EBOOT polls the CH374 synchronously and does
  not consume the interrupt. The GPIO pin that receives the CH374
  INT signal is not identified.
- The exact function that compares the typed password characters
  against the expected sequence is now identified: `maintenance_menu`
  uses a standard `memcmp` on the first 3 bytes of the input buffer
  against the hardcoded reference `{0x1D, 0x17, 0x0E}`. The earlier
  confusion about a "Duff's device dispatcher" was based on analysis
  of a corrupted EBOOT image and does not apply to the clean binary.
- Maintenance menu body (item list, per-item handlers, update paths,
  NAND programming operations): documented in
  [maintenance-mode.md](maintenance-mode.md).
- Whether the two external USB-A ports are usable by EBOOT or only by
  NK.bin. Observation suggests EBOOT's CH374 driver is scoped to the
  keyboard path only.
