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
| 0x48000000   | 0x48001FFF  | L2 Buffer SRAM (8 KB, aliases)     |
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
TX writes go to `L2_UART_TX_PORT` at 0x48001000 (an L2 buffer control/data port
in the SRAM address space; not to be confused with L2CTR_BUF8_15_CFG at
0x2002C08C, which is in the L2 controller register block). The transmit path
also clears `L2_UART_TX_FRAC_PORT` at 0x4800103C before starting TX.
RX reads come from a dynamically selected L2 buffer page. For `idx != 0`, the
address is `L2_UART_RX_PAGE_BASE + idx×4` = `0x4800107C + idx×4`, where
`idx = UART+0x08 bits [17:13]`. The bootrom special-cases `idx == 0` to use
`L2_UART_RX_PAGE0` at 0x480010FC instead of 0x4800107C.

## NAND Flash Sequencer (base 0x2002A000)

| Offset | Description                                            |
| ------ | ------------------------------------------------------ |
| +0x05C | NF timing register 0; default value 1006545 (0x0F5B51) |
| +0x060 | NF timing register 1                                   |

The BootROM-visible NF sequencer FIFO lives at 0x2002A000–0x2002A058:

| Offset         | Description                                   |
| -------------- | --------------------------------------------- |
| +0x000..+0x054 | `NF_SEQ_WORD0..NF_SEQ_WORD21`                 |
| +0x058         | `NF_SEQ_CTRL_STA`; bit 31 = sequence complete |
| +0x05C         | NF timing register 0                          |
| +0x060         | NF timing register 1                          |

There is also a second register block at 0x2002A100+, but the
BootROM NAND boot path does not use it for command sequencing.

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

The L2 buffer is an 8 KB SRAM used as an intermediate buffer for all peripheral
DMA paths (UART, USB, NAND, SPI). The physical address range is
0x48000000–0x48001FFF; accesses above 0x48001FFF alias back with a 0x2000
period (confirmed by write-readback probing).

### Physical Layout

Not all regions within the 8 KB are general-purpose SRAM. The following map
was determined empirically via USB boot mode write-readback testing:

| Offset Range  | Size   | Status | Description                                                                                                                                                 |
| ------------- | ------ | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0x000–0x03F   | 64 B   | HW     | USB EP2 bulk IN staging (L2BUF_00); written by `usb_bulk_in_send_next_chunk` during every USB TX                                                            |
| 0x040–0x1FF   | 448 B  | SRAM   | Usable                                                                                                                                                      |
| 0x200–0x23F   | 64 B   | HW     | USB EP3 bulk OUT DMA target; USB hardware writes every incoming EP3 packet here before the bootrom copies it to the stack                                   |
| 0x240–0xE6F   | 3120 B | SRAM   | Usable (largest contiguous block)                                                                                                                           |
| 0xE70–0xFFC   | 396 B  | Stack  | Bootrom stack (SP initialized to 0x48000FFC at entry); grows down through the `usbboot_main_loop` → `usb_irq_dispatch` → `handle_usbboot_packet` call chain |
| 0x1000–0x10FF | 256 B  | HW     | UART buffer / control region; includes `L2_UART_TX_PORT` (0x48001000), `L2_UART_TX_FRAC_PORT` (0x4800103C), and the UART RX page window beginning at `L2_UART_RX_PAGE_BASE` (0x4800107C) |
| 0x1100–0x157B | 1148 B | SRAM   | BootROM-proven stack/SRAM region                                                                                                                            |
| 0x157C–0x15FF | 132 B  | ?      | Not used by BootROM; do not assume usable                                                                                                                   |
| 0x1600–0x1FFF | 2560 B | HW     | Hardware-controlled region; reads return fluctuating values across runs, suggesting active DMA or controller state                                          |

**Status legend**: HW = hardware-managed (not reliably writable by CPU during
USB boot mode), Stack = bootrom stack (writable SRAM but actively used;
available after EXECUTE), SRAM = freely usable general-purpose memory.

Note: after EXECUTE hands control to a stub, the bootrom stack region
(0xE70–0xFFC) and the USB staging regions (0x000–0x03F, 0x200–0x23F) become
available. The UART and hardware-controlled regions (0x1000–0x10FF,
0x1600–0x1FFF) may remain hardware-managed depending on peripheral state.

### Named Regions

| Address Range           | Alias    | Usage                                                                                              |
| ----------------------- | -------- | -------------------------------------------------------------------------------------------------- |
| 0x48000000 - 0x4800003F | L2BUF_00 | USB EP2 bulk IN staging (64 bytes per transfer)                                                    |
| 0x48000200 - 0x4800023F | L2BUF_01 | USB EP3 bulk OUT DMA target (64 bytes); also NF/SPI read target and type-8 image execution address |
| 0x48001000              | L2_UART_TX_PORT      | UART TX data target / L2 UART buffer control port                                |
| 0x4800103C              | L2_UART_TX_FRAC_PORT | UART TX fractional/count sideband port; bootrom writes 0 before each TX          |
| 0x4800107C              | L2_UART_RX_PAGE_BASE | UART RX page base for `idx != 0`; address = 0x4800107C + idx×4                   |
| 0x480010FC              | L2_UART_RX_PAGE0     | Special-case UART RX page used when UART+0x08 bits [17:13] decode to index 0     |

### Critical: USB Boot Download to 0x48000200

When using DOWNLOAD_BEGIN to write data to 0x48000200 (L2BUF_01), the USB
hardware DMA overwrites the first 64 bytes (0x48000200–0x4800023F) with every
subsequent USB packet — including the DOWNLOAD_DONE and EXECUTE command frames.
This means the first 64 bytes of any payload downloaded to 0x48000200 are
destroyed before execution begins. Code that needs to be executed from L2
buffer should be loaded at 0x48000240 or later.

### L2 Buffer Control Registers (base 0x2002C000)

| Address    | Name               | Description                                                                 |
| ---------- | ------------------ | --------------------------------------------------------------------------- |
| 0x2002C080 | L2CTR_DMAREQ       | DMA request control                                                         |
| 0x2002C084 | L2CTR_DMA_PATH_CFG | L2 DMA path configuration; bootrom sets bits [29:28] during UART/NF init    |
| 0x2002C088 | L2CTR_BUF0_7_CFG   | L2 buffer 0..7 configuration; bit 16 = enable, bit 24 = flush               |
| 0x2002C08C | L2CTR_BUF8_15_CFG  | L2 buffer 8..15 / CPU-controlled buffer configuration; used by UART TX path |
| 0x2002C090 | L2CTR_ASSIGN_REG1  | Buffer-to-peripheral assignment; bootrom sets low 6 bits to 0x08 for USB    |
| 0x2002C094 | L2CTR_ASSIGN_REG2  | Additional assignment bits                                                  |
| 0x2002C098 | L2CTR_LDMA_CFG     | L2 DMA config                                                               |
| 0x2002C0A0 | L2CTR_STAT_REG1    | L2 buffer status register 1                                                 |
| 0x2002C0A8 | L2CTR_STAT_REG2    | L2 buffer status register 2                                                 |

Note: 0x48001000 is an L2 buffer control/data port mapped within the SRAM
address space, not in the 0x2002C000 control register block. It is distinct
from L2CTR_BUF8_15_CFG at 0x2002C08C.

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
| +0x10  | TXMAXP           | TX max packet size (indexed, 16-bit register)          |
| +0x12  | CSR0 / TXCSR1    | EP0 CSR or TX CSR1 (indexed)                           |
| +0x13  | TXCSR2           | TX CSR2 (indexed)                                      |
| +0x14  | RXMAXP           | RX max packet size (indexed, 16-bit register)          |
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
