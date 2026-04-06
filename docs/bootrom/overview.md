# AK7802 Bootrom Overview

The AK7802 bootrom is a mask ROM program (~19 KB, 0x4A60 bytes) embedded in the
SoC. It runs immediately after reset on the ARM926EJ-S core and is responsible
for selecting a boot source, loading the first-stage payload, and transferring
execution to it.

## Exception Vector Table

The ROM begins at address 0x00000000 with the standard ARM exception vector
table. The reset vector jumps directly to `bootrom_entry` at offset 0x0020.
All other vectors (Undefined Instruction, SVC, Prefetch Abort, Data Abort,
IRQ, FIQ) redirect to addresses in DDR at 0x30000004..0x3000001C, allowing
a loaded program to install its own handlers.

| Vector           | Mechanism     | Target     |
| ---------------- | ------------- | ---------- |
| Reset            | Branch        | 0x00000020 |
| Undefined Instr. | MOV PC, imm   | 0x30000004 |
| SVC              | MOV PC, imm   | 0x30000008 |
| Prefetch Abort   | MOV PC, imm   | 0x3000000C |
| Data Abort       | LDR PC, [lit] | 0x30000010 |
| Reserved         | LDR PC, [lit] | 0x30000014 |
| IRQ              | LDR PC, [lit] | 0x30000018 |
| FIQ              | LDR PC, [lit] | 0x3000001C |

## Boot Paths

The bootrom supports four operating modes, selected at entry by sampling the
DGPIO[3:2] strap pins (see [boot-flow.md](boot-flow.md)):

| DGPIO[3] | DGPIO[2] | Mode                 | Description                                     |
| -------- | -------- | -------------------- | ----------------------------------------------- |
| 0        | 0        | Normal boot          | Probe SPI, then NAND; fall back to UART console |
| 0        | 1        | USB Boot             | Enter USB download/upload/execute loop          |
| 1        | 0        | AP2-BIOS console     | Enter UART interactive console directly         |
| 1        | 1        | Diagnostic self-test | Run GPIO and RTC/USB register tests, then hang  |

## Memory Regions Used

| Address    | Size   | Description                          |
| ---------- | ------ | ------------------------------------ |
| 0x00000000 | 0x4A60 | Bootrom code and read-only data      |
| 0x08000000 | -      | System control registers (SYSCTRL)   |
| 0x20024000 | -      | SPI controller registers             |
| 0x20026000 | -      | UART controller registers            |
| 0x2002A000 | -      | NAND Flash sequencer registers       |
| 0x2002B000 | -      | NAND Flash ECC/DMA registers         |
| 0x48000000 | 0x1800 | L2 buffer SRAM                       |
| 0x70000000 | -      | USB controller registers (MUSBMHDRC) |
| 0x30000000 | -      | DDR SDRAM base (external memory)     |

See [memory-map.md](memory-map.md) for a full register-level breakdown.

## Stage Progression Marker

The bootrom writes a stage code to the RTC boot-mode register at SYSCTRL+0x54
(`rRTC_BOOTMOD`) as it progresses through each phase. This allows post-mortem
diagnosis of how far the boot process advanced:

| Value      | Phase                             |
| ---------- | --------------------------------- |
| 0x01000000 | USB Boot mode entered             |
| 0x02000000 | AP2-BIOS console mode entered     |
| 0x03000000 | SPI boot probe started            |
| 0x04000000 | NAND boot probe started           |
| 0x05000000 | Diagnostic self-test mode entered |

## Execution Handoff

When a valid boot image is found, the bootrom jumps to one of two fixed
addresses depending on the image type:

- **0x48000200** (L2 buffer) for image type 8, used by small in-place payloads
- **0x30000000** (DDR base) for image type 6, used by payloads that require
  DDR initialization via the image's embedded register init script

The bootrom does not return. All paths either jump to a loaded payload or loop
forever (diagnostic mode / UART console).
