# Memory Map and Register Reference

This document lists the memory regions and registers that EBOOT uses beyond
the set already documented in [docs/bootrom/memory-map.md](../bootrom/memory-map.md).
The bootrom document is assumed as a prerequisite; entries already cataloged
there are not repeated here unless EBOOT adds new fields or EBOOT-specific
usage.

## Address Space Overview (EBOOT additions)

| Base Address | End Address | Region                                     |
| ------------ | ----------- | ------------------------------------------ |
| 0x20010000   | 0x200100FF  | LCD controller                             |
| 0x20024000   | 0x20024023  | SPI controller block used by CH374 and ENC28J60 |
| 0x20025000   | 0x20025023  | Second SPI-controller slot reachable through `spi_init_controller(1)` |
| 0x30000000   | 0x33FFFFFF  | DDR SDRAM, 64 MB, wraps at 64 MB boundary  |

Current EBOOT code actively uses physical `0x20024000`. `ch374_init` reaches
it through `spi_init_controller(0)`, while `enc28j60_init` hard-codes the same
base directly. `spi_init_controller(1)` can also select `0x20025000`, but no
caller in this image uses that path. The vendor numbering of these SPI blocks
is therefore left unresolved here.

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
M  = PLL[20:17]         # pre-divider - 1

VCO       = 4 MHz * (N + 62)
pre_div   = VCO / (M + 1)
CPU_CLK   = (P == 0 ? pre_div / 2 : pre_div / (1 << P))
```

Example: the typical 248 MHz configuration is `N=62, M=0, P=0`:
`VCO = 4 * (62 + 62) = 496 MHz`, `pre_div = 496 MHz`,
`CPU_CLK = 496 / 2 = 248 MHz`.

## SPI Controller Register Usage

Current EBOOT code reaches the SPI hardware through two paths. The CH374 path
calls `spi_init_controller(0)` and then uses the generic helpers
`spi_transfer_tx`, `spi_transfer_txrx`, and `spi_config_mode_clock`; the
ENC28J60 path programs physical `0x20024000` directly inside `enc28j60_init`.
The unused `spi_init_controller(1)` branch selects `0x20025000`, but no caller
in this image exercises it.

The register offsets that are directly visible in code are:

| Offset | Width | Observed use |
| ------ | ----- | ------------ |
| +0x00  | 32    | Control / mode word |
| +0x04  | 32    | Status |
| +0x0C  | 16    | Transfer count |
| +0x10  | 32    | Cleared by the generic TX helper before write bursts |
| +0x14  | 32    | Cleared by the generic TX/RX helper before the read phase |
| +0x18  | 32    | TX data port |
| +0x1C  | 32    | RX data port |
| +0x20  | 32    | Written to `0x00FFFFFF` by both init paths `[partial]` |

In the ENC28J60 path, `enc28j60_init` computes a divider that keeps the SPI
clock at or below 10 MHz, then programs `SPI_CTRL = (div << 8) | 0x52` and
`SPI_CONFIG2 = 0x00FFFFFF`. For a 248 MHz CPU clock the code first computes
`div = 11`, then bumps it to `12`, giving
`248 / (2 * (12 + 1)) = 9.54 MHz`.

The ENC28J60 transfer code polls status bit `2` while filling `+0x18`, status
bit `6` while draining `+0x1C`, and status bit `8` to wait for transfer
completion. Those bit meanings are directly supported by the driver's control
flow.

The generic SPI helper used by CH374 toggles control bits `0` and `1` around
the TX/RX phase split and uses control bit `5` in `spi_cs_assert` /
`spi_cs_deassert`. The exact semantic names of those bits are not yet unified
with the ENC28J60-side view, so this document records only the operations that
are directly visible in code.

## DDR Runtime Layout

DDR SDRAM is 64 MB at physical `0x30000000..0x33FFFFFF`. The broader
platform analysis and the working LCD configuration indicate a wrap at
the 64 MB boundary. From EBOOT assembly alone, the directly visible
facts are the cached framebuffer clear at `0x87B00000` and the LCD base
register literal `0x07B00000`; the effective `0x33B00000` DMA source is
an inference from that wrap behavior.

EBOOT's default DDR runtime layout:

| Region                  | Address                   | Purpose                                          |
| ----------------------- | ------------------------- | ------------------------------------------------ |
| EBOOT IRQ stack top     | 0x30FFFF00                | IRQ-mode stack initialized early in relocation   |
| EBOOT SVC stack top     | 0x30036000                | SVC-mode stack for the main EBOOT code           |
| IMG wrapper             | 0x30037FD4 - 0x30037FFF   | 44-byte IMG header copied from NAND by nboot     |
| EBOOT code and data     | 0x30038000+               | EBOOT `.text` / `.data` / `.bss`                 |
| NK load target          | 0x30200000 (virt 0x80200000) | WinCE kernel loaded here by EBOOT before jump |
| Framebuffer             | 0x33B00000                | Effective wrapped LCD DMA source on current hardware |
| Top of DDR              | 0x33FFFFFF                | End of the 64 MB window                          |

EBOOT writes the literal `0x07B00000` into the LCD controller's base
register. On the current 64 MB board this corresponds to the effective
DMA source conventionally described as `0x33B00000`; see
[lcd-driver.md](lcd-driver.md) for the distinction between the assembly
facts and the address-wrap interpretation.

## Virtual Address Mapping

EBOOT uses a baked-in WinCE-style OEMAddressTable through `OALPAtoVA`,
mapping peripherals and DDR into two virtual regions:

- `0x8xxx_xxxx`: cached alias
- `0xAxxx_xxxx`: uncached alias (same offsets as cached, different base)

All register accesses inside EBOOT use the uncached alias after calling
`OALPAtoVA(phys, cached=0)`. Examples:

| Physical     | Uncached Virtual  | Cached Virtual   | Region         |
| ------------ | ----------------- | ---------------- | -------------- |
| 0x08000000   | 0xA8100000        | 0x88100000       | SYSCTRL        |
| 0x20010000   | 0xA8010000        | 0x88010000       | LCD controller |
| 0x20024000   | 0xA8024000        | 0x88024000       | SPI controller |
| 0x2002A000   | 0xA802A000        | 0x8802A000       | NAND sequencer |
| 0x30000000   | 0xA0000000        | 0x80000000       | DDR SDRAM      |
| 0x48000000   | 0xA8200000        | 0x88200000       | L2 SRAM        |

The exact base pairs are not tabulated across every region in this document;
the list above is the set directly confirmed in the current analysis.
`OALPAtoVA` is the OEM hook and returns the correct virtual address given the
physical.

The convention that DDR's uncached alias begins at `0xA0000000` is relevant
for one runtime data structure: EBOOT stores the active network state
(device IP and subnet mask) inside DDR at uncached virtual `0xA0020838`,
which corresponds to DDR physical `0x30020838`.

## Global Variables of Interest

EBOOT uses a small set of fixed-address globals in `.data` / `.bss`. The
important ones:

| Address      | Contents                                                               |
| ------------ | ---------------------------------------------------------------------- |
| 0x80104A5C   | ENC28J60 SPI virtual base pointer                                      |
| 0x80104A60   | Cached ENC28J60 bank-select bits (`0x00`, `0x20`, `0x40`, `0x60`)      |
| 0x80107768   | Generic SPI virtual base pointer used by the CH374 path                |
| 0x80107798   | Selected generic SPI controller index (`0` -> `0x20024000`, `1` -> `0x20025000`) |
| 0x80106E14   | SYSCTRL virtual base pointer (populated by early init)                 |
| 0x80106E40   | First slot of the Ethernet HAL dispatch block                          |
| 0x80106E44   | Vtable: RX-ready helper                                                |
| 0x80106E4C   | Vtable: receive function                                               |
| 0x80106E54   | Vtable: driver init function                                           |
| 0x80106E58   | Vtable: send function                                                  |
| 0x80106E60   | Backend-private field zeroed by Ethernet registration paths            |
| 0x80106EA0   | Start of the in-RAM default PTB structure                              |
| 0x80106EB0   | Device IP address (u32 little-endian; default `0x0B00A8C0`)            |
| 0x80106EB4   | Subnet mask (u32 little-endian; default `0x00FFFFFF`)                  |
| 0x80106EB8   | Gateway IP (u32 little-endian; default `0`)                            |
| 0x800F0140   | Runtime copy of the 57-entry alt-function dispatch table               |
| 0x800F36B0   | ENC28J60 cached `next_packet_ptr` (see `ethernet-driver.md`)           |
| 0x800F5128   | Fixed Ethernet RX frame buffer base passed to `OEMEthGetFrame`         |
| 0x800F5134   | EtherType field inside that RX buffer (`0x800F5128 + 12`)              |
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
- The vendor numbering of the SPI controller blocks is not fixed here.
  Current EBOOT code actively uses physical `0x20024000`, and
  `spi_init_controller(1)` names `0x20025000`, but the correspondence to any
  external `SPI0` / `SPI1` / `SPI2` naming is not yet confirmed.
- SPI `+0x20`: written to `0x00FFFFFF` by both the generic SPI init path and
  the ENC28J60 init path, but its exact purpose is still unknown.
- The generic SPI helper's use of control bits `0`, `1`, and `5`, and the
  ENC28J60 driver's fixed low-byte mode value `0x52`, have not yet been
  reconciled into a single register-level model.
- The exact contents of the full `OEMAddressTable` are not reproduced here;
  only the entries observed through `OALPAtoVA` calls are listed.
