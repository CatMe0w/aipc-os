# nboot Overview

nboot is the first-stage bootloader of AIPC netbook, residing in NAND flash.
It is loaded and entered by the AK7802 bootrom, initializes the NAND flash
controller using parameters embedded in the image, reads eboot from NAND into
DDR, and hands off execution to it.

## Binary Properties

| Property       | Value                                           |
| -------------- | ----------------------------------------------- |
| NAND partition | NBT (block 0, 2 blocks)                         |
| Loaded by      | AK7802 bootrom (image type 6)                   |
| Entry point    | `0x30000000` (`nboot_relocate_and_enter`)       |
| Image size     | 0x3000 bytes (padded); actual code ~0xD00 bytes |
| Architecture   | ARM926EJ-S, 32-bit, little-endian               |

At least two firmware versions exist: **v1.58.2** and **v1.88**. The boot flow
and structure are the same; specific behavioral differences are noted where known.

## Role in the Boot Chain

```
AK7802 bootrom
    │  executes DDR init script embedded in nboot image header
    │  loads nboot ARM payload -> DDR 0x30000000
    └─> nboot_relocate_and_enter (0x30000000)
            │  copies self to 0x30E00000
            └─> nboot_main (0x30E000CC)
                    │  initializes NAND controller
                    │  loads IPL raw bytes -> DDR 0x30037FD4
                    └─> eboot entry (0x30038000)
```

The AK7802 bootrom treats nboot as a type-6 image: it first executes the DDR
SDRAM initialization register script embedded in the image header, then loads
the ARM payload into DDR at `0x30000000`, and jumps there. See
[boot-flow.md](boot-flow.md) for the step-by-step flow with register-level
detail.

## Memory Layout

| Address range           | Contents                                       |
| ----------------------- | ---------------------------------------------- |
| `0x30000000-0x30000CFF` | nboot ARM payload (initial load by bootrom)    |
| `0x30036000`            | SVC mode stack pointer (set by nboot)          |
| `0x30037FD4`            | IPL container load start (`IMG` header included) |
| `0x30038000`            | eboot handoff / first payload instruction      |
| `0x30E00000-0x30E00CFF` | nboot relocated copy (runs from here)          |
| `0x30E00064`            | NAND parameter table (embedded in nboot image) |
| `0x30E00D00-0x30E00D13` | Runtime NAND parameter variables               |
| `0x30FFFF00`            | IRQ mode stack pointer (set by nboot)          |
