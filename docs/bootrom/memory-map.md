# Memory Map and Register Reference

This document lists the memory-mapped registers and the SRAM regions that the bootrom accesses.

## Address Space Overview

| Base Address | End Address | Region |
| --- | --- | --- |
| 0x00000000 | 0x00004A60 | Bootrom (mask ROM) |
| 0x08000000 | 0x080000FF+ | System Control Registers (SYSCTRL) |
| 0x20020000 | 0x2002003F | MMC/SD Host Controller (PL180-like) |
| 0x20024000 | 0x2002401F | SPI Controller |
| 0x20026000 | 0x2002600F | UART Controller |
| 0x2002A000 | 0x2002A06F | NAND Flash Sequencer |
| 0x2002B000 | 0x2002B00F | NAND Flash ECC/DMA Control |
| 0x30000000 | - | DDR SDRAM (external memory) |
| 0x48000000 | 0x4800157F | L2 Buffer SRAM (5.375 KB, 5504 B, aliases from 0x48002000) |
| 0x70000000 | 0x70000FFF | USB Controller (MUSBMHDRC-like) |

## Incomplete Address Decoding

The MMC/SD block decodes only the low eight bits of the address. Its register file is 0x40 bytes and repeats every 0x100 through the 4 KB around it. The block ignores every address bit above the low eight. A word read at 0x2F020000 returns the same sixteen words as a word read at 0x20020000, byte for byte.

This is a second reason why a read of nominally empty address space misleads, in addition to the byte lane restriction below. The data can come from a peripheral somewhere else, not from the address in the request.

## Faulting Accesses Do Not Fault

With the MMU off, this part aborts on neither of the two errors that normally catch a bad access.

A byte read from an address outside any peripheral or memory region returns zero instead of an abort. These addresses all read as zero, and none of them raises a data abort: 0x10000000, 0x20000000, 0x40000000, 0x50000000, 0x60000000, 0x80000000, 0x90000000, 0xA0000000, 0xC0000000, 0xE0000000, 0xF0000000 and 0xFFFF0000.

Those measurements say nothing about the result of a word read, because not every region answers on all four byte lanes. In the MMC/SD block a word read gives 0x4C313647 where byte reads assemble 0x00000047. Only lane 0 answers, and the other three read as zero. SYSCTRL does answer on all four. The restriction is therefore per region, and a byte read alone does not show it.

An unaligned load does not abort either, so CP15 alignment checking is off. An `LDR` from an address one byte past a word boundary returns the aligned word rotated right by eight bits. This is the ARMv5 behavior with the check disabled. A read of 0x33500001, where 0x33500000 holds 0xE5910000, gives 0x00E59100.

In practice a stray pointer or a misaligned access gives data that looks plausible instead of a crash, and the program continues. Such an access reaches neither the data abort vector nor the prefetch abort vector, thus a handler on either one does not see these mistakes.

## Exception Vector Table

The ARM vectors sit at 0x00000000, inside the mask ROM, and nothing can write them. Every vector except reset forwards to a word in DDR, thus the code loaded there selects the live handlers, not the ROM.

| Vector | Instruction at 0x0 | Forwards to |
| --- | --- | --- |
| 0x00 reset | `b 0x20` | stays in ROM |
| 0x04 undefined | `mov pc, #0x30000004` | 0x30000004 |
| 0x08 SWI | `mov pc, #0x30000008` | 0x30000008 |
| 0x0C prefetch abort | `mov pc, #0x3000000C` | 0x3000000C |
| 0x10 data abort | `ldr pc, [pc, #0xD0]` | 0x30000010 |
| 0x14 reserved | `ldr pc, [pc, #0xD0]` | 0x30000014 |
| 0x18 IRQ | `ldr pc, [pc, #0xD0]` | 0x30000018 |
| 0x1C FIQ | `ldr pc, [pc, #0xD0]` | 0x3000001C |

The two forms differ only in the way the ROM encodes the destination. The first three carry it as a rotated immediate. The rest load it from a literal at 0x000000E8 through 0x000000F4. In both forms the destination is one word of DDR that holds one instruction, and that instruction branches to the real handler.

The vectors are the low ones, not the high ones at 0xFFFF0000. An undefined instruction reaches its handler when 0x30000004 points to one. The banked `lr` on entry holds the trapping address plus four, as the architecture specifies.

No stage of the boot chain claims this table. openNBOOT loads at 0x30000000 and its entry code occupies the same words, thus an image that needs exceptions must overwrite them.

## System Control Registers (SYSCTRL, base 0x08000000)

| Offset | Bootrom Usage |
| --- | --- |
| +0x04 | Clock divider and PLL change enable. The DDR init script writes it. |
| +0x0C | Module clock gates in the low half, per-module software reset in the high half. The bootrom writes 0x59DB at entry. |
| +0x18 | Clock PLL configuration. `delay_ticks` writes `(12*n) \| 0xC000000` and waits on bit 29. |
| +0x34 | Module IRQ mask. The bootrom does not touch it. nboot clears it. |
| +0x38 | Module FIQ mask. The bootrom does not touch it. nboot clears it. |
| +0x50 | RTC/USB indexed sideband write register (see [diag-mode.md](diag-mode.md)) |
| +0x4C | RTC/USB sideband status. Bit 24 = transfer-done flag. |
| +0x54 | Boot stage marker and RTC sideband read-back |
| +0x58 | USB control. The low 3 bits are a field. The bootrom clears them, then sets the field to 6 (`0b110`), which enables the USB block. |
| +0x74 | Sharepin config register 0. It selects the peripheral function against the GPIO function. |
| +0x78 | Sharepin config register 1. It holds more mux bits and the UART enable bit. |
| +0x7C | GPIO1 direction register |
| +0x80 | GPIO1 output data register |
| +0x84 | GPIO2 direction register |
| +0x88 | GPIO2 output data register |
| +0x8C | GPIO3 direction register |
| +0x90 | GPIO3 output data register |
| +0x94 | GPIO4 direction register |
| +0x98 | GPIO4 output data register |
| +0x9C | GPIO1 pull-up/pull-down register |
| +0xA0 | GPIO2 pull-up/pull-down register |
| +0xA4 | GPIO3 pull-up/pull-down register |
| +0xA8 | GPIO4 pull-up/pull-down register. The DDR init script writes it. |
| +0xBC | GPIO1 input data register (read-only) |
| +0xC0 | GPIO2 input data register (read-only) |
| +0xC4 | GPIO3 input data register (read-only) |
| +0xC8 | GPIO4 input data register (read-only). Bits 6:5 = DGPIO[3:2] strap. |
| +0xCC | Module interrupt status register. Bit 25 = USB event pending. |
| +0xD4 | I/O control register. The bootrom sets bits [17:2] and [27:26]. |
| +0xDC | PLL N parameter (`PLL = 4*M/N`). The DDR init script clears it. |

The names above are the established names for these offsets. The bootrom itself shows only the accesses, not the names. Two of these registers matter for any code that replaces nboot.

`+0x0C` gates the module clocks. A clear bit enables the clock of a module, and a set bit disables it. The entry value 0x59DB of the bootrom therefore gates off the modules that the bootrom does not need. The write of zero from nboot turns every module clock back on and releases every module reset. EBOOT can then address the LCD, NAND and MMC blocks without a write to any clock gate.

`+0x34` and `+0x38` mask the module IRQ and FIQ sources. The polarity is not certain, because two readings of these registers exist. One reading has 1 = mask. The other has 1 = unmask and 0 = mask, and it writes zero to both registers at init to mask everything. Only the second reading matches the behavior of code that runs on the part, and this document keeps to it. The write of zero from nboot therefore masks all module interrupt sources. That reading also explains how nboot runs with the CPU IRQ and FIQ bits clear and no exception vectors installed.

## SPI Controller (base 0x20024000)

| Offset | Description |
| --- | --- |
| +0x00 | SPI control register. Bit fields: CS(0), read-enable(0), speed divider, mode. |
| +0x04 | SPI status register. Bit 8 = transfer-complete flag. |
| +0x0C | SPI transfer count, the number of bytes to transfer |
| +0x18 | SPI TX data register (write one byte) |
| +0x1C | SPI RX data register (read 32-bit word) |

## UART Controller (base 0x20026000)

| Offset | Description |
| --- | --- |
| +0x00 | UART control register. Bit 23 = RX enable, bit 28 = TX start. |
| +0x04 | UART status and control register. Bit 30 = RX data available, bit 2 = fractional-read flag. The bootrom also writes 0x10010 here at TX start. |
| +0x08 | UART config and count register. Bits [17:13] = L2 buffer index for RX, bits [24:23] = fractional byte count, bits [12:0] = TX remaining count. |

The UART moves data through L2 buffer SRAM, not through a dedicated FIFO. TX writes go to `L2_UART_TX_PORT` at 0x48001000, an L2 buffer control and data port in the SRAM address space. Do not confuse it with `L2CTR_BUF8_15_CFG` at 0x2002C08C, which is in the L2 controller register block. The transmit path also clears `L2_UART_TX_FRAC_PORT` at 0x4800103C before it starts TX. RX reads come from an L2 buffer page that the hardware selects. For `idx != 0` the address is `L2_UART_RX_PAGE_BASE + idx*4` = `0x4800107C + idx*4`, where `idx = UART+0x08 bits [17:13]`. For `idx == 0` the bootrom reads `L2_UART_RX_PAGE0` at 0x480010FC instead of 0x4800107C.

## NAND Flash Sequencer (base 0x2002A000)

| Offset | Description                                            |
| ------ | ------------------------------------------------------ |
| +0x05C | NF timing register 0. Default value 1006545 (0x0F5BD1) |
| +0x060 | NF timing register 1                                   |

Both timing registers pack five four-bit fields as `(opt_len << 16) | (we_fe << 12) | (we_re << 8) | (re_fe << 4) | re_re`. `opt_len` is the total cycle length. The other four fields place the falling and rising edges of WE and RE inside that length. When a requested length does not fit the fields, the established fallback value is 0xF5C5C. EBOOT writes that same constant to timing register 1 on both firmware versions.

The NF sequencer FIFO that the bootrom uses lives at 0x2002A000-0x2002A058:

| Offset         | Description                                   |
| -------------- | --------------------------------------------- |
| +0x000..+0x054 | `NF_SEQ_WORD0..NF_SEQ_WORD21`                 |
| +0x058         | `NF_SEQ_CTRL_STA`. Bit 31 = sequence complete |
| +0x05C         | NF timing register 0                          |
| +0x060         | NF timing register 1                          |

A second register block with the same layout sits at 0x2002A100, and nboot and EBOOT use that one instead. The FIFO occupies `+0x00..+0x4C`, twenty words. `+0x50` receives the result of an ID read or a status read. `+0x58` is the control and status register. `+0x5C` and `+0x60` are the two timing registers. A sequence therefore cannot be longer than twenty micro-operations.

### Micro-op Encoding

The NF block is a command sequencer, not a command register. One NAND bus transaction becomes a set of micro-operations in successive FIFO words. One write to the control register then runs them and drives CLE, ALE, WE and RE in order. Each word holds:

| Bits | Field | Meaning |
| --- | --- | --- |
| [21:11] | payload | address byte, command byte, transfer count minus one, or delay tick count |
| 10 | `CMD_WAIT` | delay after this cycle, tick count from the payload |
| 9 | `RBN_EN` | wait for R/B to return from busy to ready |
| 8 | `DAT_EN` | route through the data path |
| 7 | `STAFF_EN` |  |
| 6 | `CMD_EN` | drive one byte onto the IO bus |
| 5 | `WEN` | write enable |
| 4 | `REN` | read enable |
| 3 | `CNT_EN` | the payload is a count, not a single byte |
| 2 | `CLE` | command latch enable |
| 1 | `ALE` | address latch enable |
| 0 | `CMD_END` | last micro-op of the sequence |

These field names are the established ones for this sequencer, and the same cycle types carry the same names elsewhere. Every value that the bootrom, nboot and EBOOT write decomposes cleanly under them:

| Value | Composition                 | Cycle                          |
| ----- | --------------------------- | ------------------------------ |
| 0x062 | `ALE` + `WEN` + `CMD_EN`    | output one address byte        |
| 0x064 | `CLE` + `WEN` + `CMD_EN`    | output one command byte        |
| 0x118 | `CNT_EN` + `REN` + `DAT_EN` | read payload+1 bytes           |
| 0x128 | `CNT_EN` + `WEN` + `DAT_EN` | write payload+1 bytes          |
| 0x058 | `CNT_EN` + `REN` + `CMD_EN` | read ID or status into `+0x50` |
| 0x200 | `RBN_EN`                    | wait for ready                 |
| 0x400 | `CMD_WAIT`                  | delay                          |
| 0x001 | `CMD_END`                   | end of sequence                |

The composite values in practice follow from these. `0x201` is wait-for-ready as the last micro-op. `0x401` is a delay as the last micro-op, with the tick count in the payload. `0x00107919` is `(527 << 11) | 0x118 | 0x001`, a final 528-byte read. `0x00018064` outputs command byte 0x30. The raw read path of nboot writes `0x00018464` for the same command byte and adds `CMD_WAIT`. Its ECC path relies on the wait-for-ready that comes next instead. Both sequences end with `0x201`, thus the extra delay is redundant, not load-bearing.

The command bytes themselves are the standard ones: 0x00 and 0x30 to read a page, 0x01 to address the second half of a small page, 0x80 and 0x10 to program, 0x60 and 0xD0 to erase, 0x70 to read status, 0x90 to read the ID, 0xFF to reset.

## NAND Flash ECC/DMA Control (base 0x2002B000)

| Offset | Description |
| --- | --- |
| +0x00 | DMA control register. It encodes the byte count, the transfer direction, and the buffer assignment. Bit 6 = transfer-done flag (write 1 to clear). |

## L2 Buffer SRAM (base 0x48000000)

The L2 buffer is a 5.375 KB (5504 bytes) SRAM. Every peripheral DMA path uses it as an intermediate buffer: UART, USB, NAND and SPI. The connected address range is 0x48000000-0x4800157F. Addresses 0x48001580-0x48001FFF are not connected, thus reads there return noise and writes have no effect. Accesses from 0x48002000 upward alias back with a period of 0x2000. Write-readback probing confirms this.

### Buffer Structure

The SRAM holds buffers of unequal size, back to back. Each buffer takes its own assignment:

| Index | Count | Size  | Address range           |
| ----- | ----- | ----- | ----------------------- |
| 0-7   | 8     | 512 B | 0x48000000 - 0x48000FFF |
| 8-15  | 8     | 128 B | 0x48001000 - 0x480013FF |
| 16    | 1     | 256 B | 0x48001400 - 0x480014FF |
| 17-18 | 2     | 64 B  | 0x48001500 - 0x4800157F |

This is exactly the 5504 connected bytes, and it explains the register split. `L2CTR_BUF0_7_CFG` at 0x2002C088 covers the eight 512-byte buffers. `L2CTR_BUF8_15_CFG` at 0x2002C08C covers the eight 128-byte buffers. The UART port at 0x48001000 is buffer 8.

In the 512-byte group, the enable bit for buffer `n` is bit `16 + n` of 0x2002C088 and its flush bit is bit `24 + n`. The fill level of buffer `n` is a four-bit field at bits `[4n+3:4n]` of `L2CTR_STAT_REG1` (0x2002C0A0). The NAND path of nboot exercises all three for buffer 5 and drains the data from 0x48000A00, which confirms the `base + index * 512` mapping.

### Physical Layout

Not every region in the 8 KB is general-purpose SRAM. Write-readback tests in USB boot mode gave this map:

| Offset Range | Size | Status | Description |
| --- | --- | --- | --- |
| 0x000-0x03F | 64 B | HW | USB EP2 bulk IN staging (L2BUF_00). `usb_bulk_in_send_next_chunk` writes it during every USB TX. |
| 0x040-0x1FF | 448 B | SRAM | Usable |
| 0x200-0x23F | 64 B | HW | USB EP3 bulk OUT DMA target. The USB hardware writes every incoming EP3 packet here before the bootrom copies it to the stack. |
| 0x240-0xE6F | 3120 B | SRAM | Usable, the largest contiguous block |
| 0xE70-0xFFC | 396 B | Stack | Bootrom stack. SP starts at 0x48000FFC at entry and grows down through the `usbboot_main_loop` -> `usb_irq_dispatch` -> `handle_usbboot_packet` call chain. |
| 0x1000-0x10FF | 256 B | HW | UART buffer and control region. It holds `L2_UART_TX_PORT` (0x48001000), `L2_UART_TX_FRAC_PORT` (0x4800103C), and the UART RX page window from `L2_UART_RX_PAGE_BASE` (0x4800107C). |
| 0x1100-0x157F | 1152 B | SRAM | Usable |
| 0x1580-0x1FFF | 2688 B | NC | Not connected. Reads return noise, writes have no effect. |

**Status legend**: HW = hardware-managed, that is, not reliably writable by the CPU in USB boot mode. Stack = the bootrom stack, writable SRAM but in use, and free after EXECUTE. SRAM = general-purpose memory, free to use. NC = not connected, reads noise, writes ineffective.

After EXECUTE gives control to a stub, the stack region (0xE70-0xFFC) and the USB staging regions (0x000-0x03F, 0x200-0x23F) become free. The UART region (0x1000-0x10FF) can stay hardware-managed, which depends on the peripheral state. The range 0x1580-0x1FFF is not connected and stays unusable in every peripheral state.

### Write Granularity

The L2 buffer SRAM does not accept byte writes. A `STRB` to any address in the L2 region copies the byte across the full aligned 32-bit word. A `STRB` of 0x78 to address 0x48000240, for example, gives 0x78787878 at the aligned word 0x48000240-0x48000243. All writes to the L2 buffer must use word-width store instructions (`STR`).

### Named Regions

| Address Range | Alias | Usage |
| --- | --- | --- |
| 0x48000000 - 0x4800003F | L2BUF_00 | USB EP2 bulk IN staging (64 bytes per transfer) |
| 0x48000200 - 0x4800023F | L2BUF_01 | USB EP3 bulk OUT DMA target (64 bytes). Also the NF and SPI read target, and the type-8 image execution address. |
| 0x48001000 | L2_UART_TX_PORT | UART TX data target and L2 UART buffer control port |
| 0x4800103C | L2_UART_TX_FRAC_PORT | UART TX fractional and count sideband port. The bootrom writes 0 here before each TX. |
| 0x4800107C | L2_UART_RX_PAGE_BASE | UART RX page base for `idx != 0`. Address = 0x4800107C + idx*4. |
| 0x480010FC | L2_UART_RX_PAGE0 | UART RX page for the special case where UART+0x08 bits [17:13] decode to index 0 |

These names are older than the buffer structure above. They name the part of a buffer that one peripheral touches, not a whole buffer. `L2BUF_00` and `L2BUF_01` are the first 64 bytes of buffers 0 and 1. The UART ports sit in buffers 8 and 9. The NAND path uses whole buffers: buffer 0 for the bootrom, buffer 5 (0x48000A00) for nboot.

### Critical: USB Boot Download to 0x48000200

When DOWNLOAD_BEGIN writes data to 0x48000200 (L2BUF_01), the USB hardware DMA overwrites the first 64 bytes (0x48000200-0x4800023F) with every later USB packet. The DOWNLOAD_DONE and EXECUTE command frames do the same. The first 64 bytes of a payload at 0x48000200 are therefore lost before execution starts. Load code that must run from the L2 buffer at 0x48000240 or higher.

### L2 Buffer Control Registers (base 0x2002C000)

| Address | Name | Description |
| --- | --- | --- |
| 0x2002C080 | L2CTR_DMAREQ | DMA request control |
| 0x2002C084 | L2CTR_DMA_PATH_CFG | L2 DMA path configuration. The bootrom sets bits [29:28] during UART and NF init. |
| 0x2002C088 | L2CTR_BUF0_7_CFG | L2 buffer 0..7 configuration. Bit 16 = enable, bit 24 = flush. |
| 0x2002C08C | L2CTR_BUF8_15_CFG | L2 buffer 8..15 and CPU-controlled buffer configuration. The UART TX path uses it. |
| 0x2002C090 | L2CTR_ASSIGN_REG1 | Buffer-to-peripheral assignment. The bootrom sets the low 6 bits to 0x08 for USB. |
| 0x2002C094 | L2CTR_ASSIGN_REG2 | More assignment bits |
| 0x2002C098 | L2CTR_LDMA_CFG | L2 DMA config |
| 0x2002C0A0 | L2CTR_STAT_REG1 | L2 buffer status register 1 |
| 0x2002C0A8 | L2CTR_STAT_REG2 | L2 buffer status register 2 |

0x48001000 is an L2 buffer control and data port in the SRAM address space, not in the 0x2002C000 control register block. It is not `L2CTR_BUF8_15_CFG` at 0x2002C08C.

## USB Controller (base 0x70000000)

The USB controller is a MUSBMHDRC-compatible core. The register offsets follow the standard MUSBMHDRC layout:

| Offset | Name | Description |
| --- | --- | --- |
| +0x00 | FADDR | Function address, set after SET_ADDRESS |
| +0x01 | POWER | Power management. Bit 0 = enable suspend. |
| +0x02 | INTRTX1 | TX endpoint interrupt status. Bit 0 = EP0, bit 2 = EP2. |
| +0x04 | INTRRX1 | RX endpoint interrupt status. Bit 3 = EP3. |
| +0x06 | INTRTX1E | TX interrupt enable mask |
| +0x08 | INTRRX1E | RX interrupt enable mask |
| +0x0A | INTRUSB | USB system interrupt status. Bit 2 = bus reset. |
| +0x0B | INTRUSBE | USB system interrupt enable |
| +0x0E | INDEX | Endpoint index select register |
| +0x10 | TXMAXP | TX max packet size (indexed, 16-bit register) |
| +0x12 | CSR0 / TXCSR1 | EP0 CSR or TX CSR1 (indexed) |
| +0x13 | TXCSR2 | TX CSR2 (indexed) |
| +0x14 | RXMAXP | RX max packet size (indexed, 16-bit register) |
| +0x16 | RXCSR1 | RX CSR1 (indexed) |
| +0x18 | COUNT0 / RXCOUNT | EP0 byte count or RX byte count (indexed) |
| +0x20 | FIFO EP0 | EP0 FIFO access port |
| +0x28 | FIFO EP2 | EP2 FIFO access port |

### Vendor-Specific USB Registers

| Address    | Description                                         |
| ---------- | --------------------------------------------------- |
| 0x70000330 | EP0 TX count register                               |
| 0x70000334 | EP2 TX count register                               |
| 0x70000338 | Write-forbid register, per-endpoint write gating    |
| 0x7000033C | Pre-read start register, per-endpoint DMA trigger   |
| 0x70000344 | Full-speed force register, write 1 to force FS mode |
