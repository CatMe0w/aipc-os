# Memory Map and Register Reference

This document lists the memory regions and registers that EBOOT uses beyond
the set already documented in [docs/bootrom/memory-map.md](../bootrom/memory-map.md).
The bootrom document is assumed as a prerequisite: anything it already lists
(SYSCTRL base, SPI2 at `0x20024000`, NAND sequencer, UART, L2 buffer SRAM, USB
controller, DDR SDRAM base) is not repeated here unless EBOOT adds new fields.

## Address Space Overview (EBOOT additions)

| Base Address | End Address | Region                                     |
| ------------ | ----------- | ------------------------------------------ |
| 0x20010000   | 0x200100FF  | LCD controller                             |
| 0x20020000   | 0x2002001F  | SPI0 (CH374 USB-over-SPI bridge)           |
| 0x20021000   | 0x2002101F  | SPI1 (present but unused by EBOOT)         |
| 0x2002D000   | 0x2002D00F  | DDR SDRAM controller (programmed by nboot) |
| 0x30000000   | 0x33FFFFFF  | DDR SDRAM, 64 MB, wraps at 64 MB boundary  |

The SoC implements at least three SPI controller instances. SPI0 and SPI2
are used by EBOOT; SPI1 appears present in the register map but has no
consumer in the analyzed image.

## SYSCTRL Register Additions (base 0x08000000)

EBOOT touches several SYSCTRL offsets beyond the bootrom's set. The new ones
are listed below; the clock/interrupt/GPIO groups `+0x7C..+0xFC` are covered
in detail in [gpio-driver.md](gpio-driver.md).

| Offset | Usage                                                                      |
| ------ | -------------------------------------------------------------------------- |
| +0x04  | CPU PLL configuration (see *CPU Clock Formula* below)                      |
| +0x0C  | Peripheral reset / clock gate; bit 3 = LCD clock enable (inverted polarity, clear to enable), bit 19 = LCD reset pulse |
| +0x2C  | PWM high/low time: `(high_ticks << 16) | low_ticks`; base tick = 12 MHz    |
| +0x74  | Sharepin mux register 0 (bootrom docs its existence, EBOOT uses new bits)  |
| +0x78  | Sharepin mux register 1 (mixed polarity; see `gpio-driver.md`)             |
| +0x9C  | GPIO bank 0 auxiliary config (32-bit, 1 bit per GPIO1 pin) `[partial]`     |
| +0xA0  | GPIO bank 1 auxiliary config (pin - 32 -> bit) `[partial]`                 |
| +0xA4  | GPIO bank 2 auxiliary config (pin - 64 -> bit) `[partial]`                 |
| +0xA8  | GPIO bank 3 auxiliary config (pin - 96 -> bit) `[partial]`                 |
| +0xDC  | SYSCTRL soft reset; nboot's DDR init script writes 0 here                  |
| +0xE0  | GPIO1 interrupt status `[hypothesis]`                                      |
| +0xE4  | GPIO2 interrupt status `[hypothesis]`                                      |
| +0xE8  | GPIO3 interrupt status `[hypothesis]`                                      |
| +0xEC  | GPIO4 interrupt status `[hypothesis]`                                      |
| +0xF0  | GPIO1 interrupt mask, 1 = masked `[hypothesis]`                            |
| +0xF4  | GPIO2 interrupt mask `[hypothesis]`                                        |
| +0xF8  | GPIO3 interrupt mask `[hypothesis]`                                        |
| +0xFC  | GPIO4 interrupt mask `[hypothesis]`                                        |

The `+0xE0..+0xFC` block is classified as hypothesized because it is
consistent with "clear-status / mask-all" initialization performed during
early SYSCTRL setup, but no driver path in EBOOT uses interrupts.

### CPU Clock Formula

The CPU PLL configuration register at SYSCTRL+0x04 encodes the clock as:

```
N  = PLL[5:0]           # multiplier low
P  = PLL[8:6]           # post-divider exponent
HS = PLL[15]            # high-speed bypass
M  = PLL[20:17]         # pre-divider - 1

VCO       = 4 MHz * (N + 62)
pre_div   = VCO / (M + 1)
CPU_CLK   = HS ? pre_div
               : (P == 0 ? pre_div / 2 : pre_div / (1 << P))
```

Example: the typical 248 MHz configuration is `N=62, M=0, P=0, HS=0`:
`VCO = 4 * (62 + 62) = 496 MHz`, `pre_div = 496 MHz`,
`CPU_CLK = 496 / 2 = 248 MHz`.

## SPI Controller Register Maps

SPI0 and SPI2 share the same base offset layout but, importantly, their
**control-register bit assignments differ** for chip-select and transfer-start.
Code that drives one must not assume the other.

### SPI2 (base 0x20024000, ENC28J60)

Register offsets and bit meanings below are derived from the ENC28J60 driver
code. This is the canonical SPI2 register map used by EBOOT.

| Offset | Name         | Width | Description                                              |
| ------ | ------------ | ----- | -------------------------------------------------------- |
| +0x00  | SPI_CTRL     | 32    | Control register (see bit breakdown below)               |
| +0x04  | SPI_STATUS   | 32    | Status register                                          |
| +0x0C  | SPI_COUNT    | 16    | Byte count for current burst                             |
| +0x18  | SPI_TXDATA   | 32    | TX data port; writes go to the bus (4 bytes at a time)   |
| +0x1C  | SPI_RXDATA   | 32    | RX data port; reads come from the bus                    |
| +0x20  | SPI_CONFIG2  | 32    | Mode/config register; EBOOT writes `0xFFFFFF` at init `[partial]` |

`SPI_CTRL` bits:

| Bit    | Meaning                                                |
| ------ | ------------------------------------------------------ |
| 0      | CS (active low; clear = asserted)                      |
| 1      | Direction / RW strobe (set during writes)              |
| 5      | Transfer enable / hold bus active                      |
| 8..15  | Clock divider: `SPI_CLK = CPU_CLK / (2 * (div + 1))`   |

`SPI_STATUS` bits:

| Bit | Meaning                                     |
| --- | ------------------------------------------- |
| 2   | TX FIFO has space for more data             |
| 6   | RX data available                           |
| 8   | Transfer complete                           |

The clock divider is chosen to keep the SPI clock at or below 10 MHz.
For a 248 MHz CPU clock this produces a divider value around 11, giving an
actual SPI clock near 10.3 MHz.

`SPI_CONFIG2` (+0x20) is written to `0xFFFFFF` once during init and never
touched again. Its exact bit layout is not determined here; the register is
listed as partial so consumers know to not rely on specific bits.

### SPI0 (base 0x20020000, CH374)

SPI0 drives the CH374 USB host bridge chip (see `usb-hid-input.md`). It uses
the same base offset layout as SPI2 (`+0x00` ctrl, `+0x04` status, `+0x18`
tx, `+0x1C` rx), but `SPI_CTRL` bit assignments are **not** the same as
SPI2. In particular, CS and transfer-start map to different bits. The
complete SPI0 bit layout is not fully characterized in this documentation
and is marked `[partial]`.

Consumers writing new SPI0 code should not copy the SPI2 bit constants.

## DDR Runtime Layout

DDR SDRAM is 64 MB at physical `0x30000000..0x33FFFFFF`. The hardware
address decoder masks the high bits at the 64 MB boundary, so physical
addresses outside that range wrap back: e.g. a write to `0x07B00000` lands
at `0x03B00000`, which the decoder then offsets to `0x33B00000` within the
DDR window.

EBOOT's default DDR runtime layout:

| Region                  | Address                   | Purpose                                          |
| ----------------------- | ------------------------- | ------------------------------------------------ |
| EBOOT IRQ stack top     | 0x30FFFF00                | IRQ-mode stack initialized early in relocation   |
| EBOOT SVC stack top     | 0x30036000                | SVC-mode stack for the main EBOOT code           |
| IMG wrapper             | 0x30037FD4 - 0x30037FFF   | 44-byte IMG header copied from NAND by nboot     |
| EBOOT code and data     | 0x30038000+               | EBOOT `.text` / `.data` / `.bss`                 |
| NK load target          | 0x30200000 (virt 0x80200000) | WinCE kernel loaded here by EBOOT before jump |
| Framebuffer             | 0x33B00000                | 5 MB RGB565 primary surface (see `lcd-driver.md`) |
| Top of DDR              | 0x33FFFFFF                | End of the 64 MB window                          |

EBOOT writes the physical framebuffer address `0x07B00000` into the LCD
controller's base register; the 64 MB wrap places the actual DMA source at
`0x03B00000` + DDR base = `0x33B00000`.

## Virtual Address Mapping

EBOOT installs a WinCE-style OEMAddressTable that maps all peripherals and
DDR into two virtual regions:

- `0x8xxx_xxxx`: cached alias
- `0xAxxx_xxxx`: uncached alias (same offsets as cached, different base)

All register accesses inside EBOOT use the uncached alias after calling
`OALPAtoVA(phys, cached=0)`. Examples:

| Physical     | Uncached Virtual  | Cached Virtual   | Region         |
| ------------ | ----------------- | ---------------- | -------------- |
| 0x08000000   | 0xA8100000        | 0x88100000       | SYSCTRL        |
| 0x20010000   | 0xA8010000        | 0x88010000       | LCD controller |
| 0x20020000   | 0xA8020000        | 0x88020000       | SPI0           |
| 0x20024000   | 0xA8024000        | 0x88024000       | SPI2           |
| 0x2002A000   | 0xA802A000        | 0x8802A000       | NAND sequencer |
| 0x30000000   | 0xA0000000        | 0x80000000       | DDR SDRAM      |
| 0x48000000   | 0xA8200000        | 0x88200000       | L2 SRAM        |

The exact base pairs are not tabulated across every region in this document;
the list above is the set observed in EBOOT code. `OALPAtoVA` is the OEM hook
and returns the correct virtual address given the physical.

The convention that DDR's uncached alias begins at `0xA0000000` is relevant
for one runtime data structure: EBOOT stores the active network state
(device IP and subnet mask) inside DDR at uncached virtual `0xA0020838`,
which corresponds to DDR physical `0x30020838`.

## Global Variables of Interest

EBOOT uses a small set of fixed-address globals in `.data` / `.bss`. The
important ones:

| Address      | Contents                                                               |
| ------------ | ---------------------------------------------------------------------- |
| 0x80104A5C   | Current SPI virtual base pointer (SPI0 or SPI2 depending on driver)    |
| 0x80106E14   | SYSCTRL virtual base pointer (populated by early init)                 |
| 0x80106E40   | First slot of the Ethernet HAL vtable                                  |
| 0x80106E44   | Vtable: RX-ready helper                                                |
| 0x80106E4C   | Vtable: receive function                                               |
| 0x80106E54   | Vtable: driver init function                                           |
| 0x80106E58   | Vtable: send function                                                  |
| 0x80106E60   | Cached ENC28J60 bank number (for the bank-select helper)               |
| 0x80106EA0   | Start of the in-RAM default PTB structure                              |
| 0x80106EB0   | Device IP address (u32 little-endian; default `0x0B00A8C0`)            |
| 0x80106EB4   | Subnet mask (u32 little-endian; default `0x00FFFFFF`)                  |
| 0x80106EB8   | Gateway IP (u32 little-endian; default `0`)                            |
| 0x800F0140   | Runtime copy of the 57-entry alt-function dispatch table               |
| 0x800F36B0   | ENC28J60 cached `next_packet_ptr` (see `ethernet-driver.md`)           |
| 0x800F5134   | Ethernet RX frame buffer start (written by `enc28j60_rx_poll`)         |
| 0xA0020838   | Active device IP in runtime network state (uncached DDR)               |
| 0xA002083C   | Active subnet mask in runtime network state (uncached DDR)             |

## Unresolved

- SYSCTRL `+0x9C..+0xA8` GPIO aux registers: bit-level semantics unknown.
  Observed in code as a per-pin single-bit toggle, but whether they control
  pull-up/down, input filter, drive strength, or a second pinmux layer is
  not determined.
- SYSCTRL `+0xE0..+0xFC`: hypothesized as per-bank GPIO interrupt
  status/mask based on initialization pattern alone. No EBOOT driver path
  confirms the function.
- SYSCTRL `+0xD4`: known to be used by the bank-0 input-filter path in the
  GPIO driver (via a 32-byte lookup table) and by the bootrom's diagnostic
  mode. Likely a wake-source or input-filter enable register; bit meanings
  not confirmed.
- SPI0 control register bit layout: only partially characterized. CS bit
  index is not confirmed to match SPI2.
- SPI2 `SPI_CONFIG2` (`+0x20`): written once at init, purpose unknown.
- The exact contents of the full `OEMAddressTable` are not reproduced here;
  only the entries observed through `OALPAtoVA` calls are listed.
