# Memory Map and Register Reference

This document catalogs the memory-mapped registers and SRAM regions accessed by
the bootrom.

## Address Space Overview

| Base Address | End Address | Region                             |
| ------------ | ----------- | ---------------------------------- |
| 0x00000000   | 0x00004A60  | Bootrom (mask ROM)                 |
| 0x08000000   | 0x080000FF+ | System Control Registers (SYSCTRL) |
| 0x20024000   | 0x2002401F  | SPI Controller                     |
| 0x20026000   | 0x2002600F  | UART Controller                    |
| 0x2002A000   | 0x2002A06F  | NAND Flash Sequencer               |
| 0x2002B000   | 0x2002B00F  | NAND Flash ECC/DMA Control         |
| 0x30000000   | -           | DDR SDRAM (external memory)        |
| 0x48000000   | 0x480017FF  | L2 Buffer SRAM                     |
| 0x70000000   | 0x70000FFF  | USB Controller (MUSBMHDRC-like)    |

## System Control Registers (SYSCTRL, base 0x08000000)

| Offset | Bootrom Usage                                                                                         |
| ------ | ----------------------------------------------------------------------------------------------------- |
| +0x0C  | Written with 23003 at entry; likely clock or watchdog config                                          |
| +0x18  | Clock PLL configuration; used by `sub_FA8` with formula `(12*n) \| 0xC000000` and busy-wait on bit 29 |
| +0x50  | RTC/USB indexed sideband write register (see [diag-mode.md](diag-mode.md))                            |
| +0x4C  | RTC/USB sideband status; bit 24 = transfer-done flag                                                  |
| +0x54  | `rRTC_BOOTMOD` - boot stage marker / RTC sideband read-back                                           |
| +0x58  | USB control; low 3 bits cleared then set to 6 to enable USB block                                     |
| +0x74  | Sharepin config register 0; selects peripheral vs GPIO function                                       |
| +0x78  | Sharepin config register 1; additional mux and UART enable bits                                       |
| +0x7C  | GPIO1 direction register                                                                              |
| +0x80  | GPIO1 output data register                                                                            |
| +0x84  | GPIO2 direction register                                                                              |
| +0x88  | GPIO2 output data register                                                                            |
| +0x8C  | GPIO3 direction register                                                                              |
| +0x90  | GPIO3 output data register                                                                            |
| +0x94  | GPIO4 direction register                                                                              |
| +0x98  | GPIO4 output data register                                                                            |
| +0xBC  | GPIO1 input data register (read-only)                                                                 |
| +0xC0  | GPIO2 input data register (read-only)                                                                 |
| +0xC4  | GPIO3 input data register (read-only)                                                                 |
| +0xC8  | GPIO4 input data register (read-only); bits 6:5 = DGPIO[3:2] strap                                    |
| +0xCC  | USB interrupt pending register; bit 25 = USB event pending                                            |
| +0xD4  | I/O control register; bootrom sets bits [17:2] and [27:26]                                            |

## SPI Controller (base 0x20024000)

| Offset | Description                                                                  |
| ------ | ---------------------------------------------------------------------------- |
| +0x00  | SPI control register; bit fields: CS(0), read-enable(0), speed divider, mode |
| +0x04  | SPI status register; bit 8 = transfer-complete flag                          |
| +0x0C  | SPI transfer count (number of bytes to transfer)                             |
| +0x18  | SPI TX data register (write one byte)                                        |
| +0x1C  | SPI RX data register (read 32-bit word)                                      |

## UART Controller (base 0x20026000)

| Offset | Description                                                                                                                               |
| ------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| +0x00  | UART control register; bit 23 = RX enable, bit 28 = TX start                                                                              |
| +0x04  | UART status register; bit 30 = RX data available, bit 16/17 = TX flags                                                                    |
| +0x08  | UART config/count register; bits [17:13] = L2 buffer index for RX, bits [22:23] = fractional byte count, bits [12:0] = TX remaining count |

UART data is transferred through L2 buffer SRAM rather than a dedicated FIFO.
TX writes go to L2BUF offset 0x48000E14 (dword_4800020C[893]), RX reads come
from a dynamically selected L2 buffer page based on the index in +0x08 bits
[17:13].

## NAND Flash Sequencer (base 0x2002A000)

| Offset | Description                                            |
| ------ | ------------------------------------------------------ |
| +0x05C | NF timing register 0; default value 1006545 (0x0F5B51) |
| +0x060 | NF timing register 1                                   |

The NF sequencer is controlled through a command FIFO accessible at
0x2002A100+ (aliased as `NF_SEQ_CTRL_STA`, `NF_SEQ_WORD0`, `NF_SEQ_WORD1`,
etc.). Bit 31 of the control/status register indicates sequence completion.

Each FIFO word encodes a micro-operation:

| Bits [10:0] | Encoding | Meaning                               |
| ----------- | -------- | ------------------------------------- |
| 0x62        | -        | Output address byte (data in [21:11]) |
| 0x64        | -        | Output command byte (data in [21:11]) |
| 0x119       | -        | Read data; count in [21:11]           |
| 0x401       | -        | Wait/delay; tick count in [21:11]     |

## NAND Flash ECC/DMA Control (base 0x2002B000)

| Offset | Description                                                                                                                         |
| ------ | ----------------------------------------------------------------------------------------------------------------------------------- |
| +0x00  | DMA control register; encodes byte count, transfer direction, and buffer assignment. Bit 6 = transfer-done flag (write-1-to-clear). |

## L2 Buffer SRAM (base 0x48000000)

The L2 buffer is a 6 KB SRAM used as an intermediate buffer for all peripheral
DMA paths (UART, USB, NAND, SPI).

| Address Range           | Alias    | Usage                                                                                    |
| ----------------------- | -------- | ---------------------------------------------------------------------------------------- |
| 0x48000000 - 0x480001FF | L2BUF_00 | USB EP2 bulk IN staging                                                                  |
| 0x48000200 - 0x480013FF | L2BUF_01 | Boot header/payload staging; NF/SPI read target; also the type-8 image execution address |
| 0x4800107C              | -        | UART RX L2 buffer page base                                                              |
| 0x48001500 - 0x4800150F | -        | USB EP0 setup data / TX staging                                                          |

### L2 Buffer Control Registers (base 0x2002C000)

| Address    | Alias                   | Description                                           |
| ---------- | ----------------------- | ----------------------------------------------------- |
| 0x2002C080 | L2CTR_COMBUF_CFG        | Common buffer config; bit 16 = enable, bit 24 = flush |
| 0x2002C088 | L2CTR_ASSIGN_REG1       | Buffer-to-peripheral assignment                       |
| 0x2002C08C | L2CTR_DMAFRAC           | DMA fractional transfer config                        |
| 0x2002C090 | L2CTR_ASSIGN_REG1 (alt) | Additional assignment bits                            |
| 0x48001000 | L2CTR_UARTBUF_CFG       | UART-specific L2 buffer config                        |

## USB Controller (base 0x70000000)

The USB controller is a MUSBMHDRC-compatible core. Register offsets follow
the standard MUSBMHDRC layout:

| Offset | Name             | Description                                            |
| ------ | ---------------- | ------------------------------------------------------ |
| +0x00  | FADDR            | Function address (set after SET_ADDRESS)               |
| +0x01  | POWER            | Power management; bit 0 = enable suspend               |
| +0x02  | INTRTX1          | TX endpoint interrupt status; bit 0 = EP0, bit 2 = EP2 |
| +0x04  | INTRRX1          | RX endpoint interrupt status; bit 3 = EP3              |
| +0x06  | INTRTX1E         | TX interrupt enable mask                               |
| +0x08  | INTRRX1E         | RX interrupt enable mask                               |
| +0x0A  | INTRUSB          | USB system interrupt status; bit 2 = bus reset         |
| +0x0B  | INTRUSBE         | USB system interrupt enable                            |
| +0x0E  | INDEX            | Endpoint index select register                         |
| +0x10  | TXMAXP           | TX max packet size (indexed)                           |
| +0x12  | CSR0 / TXCSR1    | EP0 CSR or TX CSR1 (indexed)                           |
| +0x13  | TXCSR2           | TX CSR2 (indexed)                                      |
| +0x14  | RXMAXP           | RX max packet size (indexed)                           |
| +0x16  | RXCSR1           | RX CSR1 (indexed)                                      |
| +0x18  | COUNT0 / RXCOUNT | EP0 byte count or RX byte count (indexed)              |
| +0x20  | FIFO EP0         | EP0 FIFO access port                                   |
| +0x28  | FIFO EP2         | EP2 FIFO access port                                   |

### Vendor-Specific USB Registers

| Address    | Description                                         |
| ---------- | --------------------------------------------------- |
| 0x70000330 | EP0 TX count register                               |
| 0x70000334 | EP2 TX count register                               |
| 0x70000338 | Write-forbid register; per-endpoint write gating    |
| 0x7000033C | Pre-read start register; per-endpoint DMA trigger   |
| 0x70000344 | Full-speed force register; write 1 to force FS mode |
