# nboot Overview

nboot is the first-stage bootloader of the AIPC netbook, and it lives in NAND flash. The AK7802 bootrom loads it and enters it. nboot initializes the NAND flash controller with parameters from its own image, reads EBOOT from NAND into DDR, and gives control to it.

## Binary Properties

| Property       | v1.58.2                                   | v1.88          |
| -------------- | ----------------------------------------- | -------------- |
| NAND partition | NBT (block 0, 2 blocks)                   | same           |
| Loaded by      | AK7802 bootrom (image type 6)             | same           |
| Entry point    | `0x30000000` (`nboot_relocate_and_enter`) | same           |
| Image size     | 0x3000 bytes padded, ~0xD00 of code       | 0x1800, ~0xDB0 |
| Architecture   | ARM926EJ-S, 32-bit, little-endian         | same           |
| Functions      | 23                                        | 21             |

Both versions are fully reverse engineered. They are the same program with a different build configuration. The boot flow, every address constant and all the algorithms match. The device-specific values that v1.58.2 hardcodes move into the parameter table of the image in v1.88. See [boot-flow.md](boot-flow.md) for the register-level flow and the full account of the differences.

The two devices carry different NAND parts. v1.58.2 runs on 64-page blocks of 128 KB, over 4096 blocks. v1.88 runs on 128-page blocks of 256 KB, over 2048 blocks. Both come to 512 MB.

## Role in the Boot Chain

```
AK7802 bootrom
    |  executes DDR init script embedded in nboot image header
    |  loads nboot ARM payload -> DDR 0x30000000
    +-> nboot_relocate_and_enter (@0x30000000)
            |  copies self to 0x30E00000
            +-> nboot_main (@0x30E000CC)
                    |  initializes NAND controller
                    |  loads IPL raw bytes -> DDR 0x30037FD4
                    +-> EBOOT entry (@0x30038000)
```

The AK7802 bootrom treats nboot as a type-6 image. It first runs the DDR SDRAM initialization register script in the image header, then loads the ARM payload into DDR at `0x30000000`, and jumps there. See [boot-flow.md](boot-flow.md) for the step-by-step flow with register-level detail.

## Scope

The whole job of nboot is to get the `IPL` partition into DDR and to branch into it. Everything else either comes from the bootrom or waits for EBOOT. nboot does not:

- parse `PTB`, or read the `IMG` header of the container that it loads. The start block, byte count, load address and entry point are all compile-time constants in `nboot_main`
- validate the loaded image in any way, by magic, by length or by checksum
- enable the MMU or the caches, or install exception vectors. It sets only the IRQ and SVC stack pointers
- touch GPIO, LCD, PMU or the clock tree. The header script that the bootrom ran already configured the clocks and DDR
- detect the NAND device. All geometry comes from a static table inside the nboot image
- give any timeout, recovery path or download fallback

## Image Layout

The payload is a flat binary with no section headers. Inside `nboot.nb0`:

| v1.58.2        | v1.88          | Contents                                 |
| -------------- | -------------- | ---------------------------------------- |
| `0x000-0x05F`  | `0x000-0x05F`  | `nboot_relocate_and_enter`               |
| `0x060`        | `0x060`        | `hang_forever` (`B .`), unreferenced     |
| `0x064-0x087`  | `0x064-0x0BF`  | NAND geometry and timing parameter table |
| `0x0AC-0x0CB`  | `0x0D8-0x0FB`  | literal pool for the relocation stub     |
| `0x0CC-0xCFF`  | `0x0FC-0xDAF`  | `nboot_main` and the rest of the code    |
| `0xD00-0x2FFF` | `0xDB0-0x17FF` | padding, not zeroed and not relocated    |

The parameter table is 9 dwords in v1.58.2 and 23 dwords in v1.88. Both images carry an unreferenced `0xE3A0F203` (`MOV PC, #0x30000000`) just before the literal pool, which matches the first word of the wrapped `akimg`.

## Memory Layout

| Address range           | Contents                                         |
| ----------------------- | ------------------------------------------------ |
| `0x30000000-0x30002FFF` | nboot image as loaded by the bootrom             |
| `0x30036000`            | SVC mode stack pointer (set by nboot)            |
| `0x30037FD4`            | IPL container load start (`IMG` header included) |
| `0x30038000`            | EBOOT handoff / first payload instruction        |
| `0x3009C7D3`            | last byte written by the EBOOT load loop         |
| `0x30E00000-0x30E00CFF` | nboot relocated copy (runs from here)            |
| `0x30E00064`            | NAND parameter table (embedded in nboot image)   |
| `0x30E00D00-0x30E00D13` | Runtime NAND parameter variables                 |
| `0x30FFFF00`            | IRQ mode stack pointer (set by nboot)            |

v1.88 relocates `0xDB0` bytes instead of `0xD00`, thus its runtime variables sit at `0x30E00DB0` through `0x30E00DFC`, with a 32-byte tag scratch at `0x30E00DC0`. Everything else in this table stays the same.

The load loop overshoots its `0x64000`-byte budget by one page. Treat the region up to `0x3009C7D3` as clobbered, not the nominal `0x3009BFD3`. See [boot-flow.md](boot-flow.md) for the reason.

## Storage Format

`NBT` is the only partition on the device with a flat layout: 2048 contiguous image bytes per physical page, with no per-chunk spare area and no BCH parity. This matches the way the bootrom reads it, with ECC off and one READ sequence per page. nboot therefore has no protection against bit rot, and no layer below EBOOT can detect or repair damage to it.

Every other partition, `IPL` included, uses the ECC-protected 528-byte chunk layout in [docs/eboot/nand-driver.md](../eboot/nand-driver.md).

## Function Addresses

The function names in these documents are our names from the reverse engineering. The nboot image holds no symbols.

nboot copies itself to `0x30E00000` before it runs, thus almost every function executes at a `0x30E0xxxx` address. Only `nboot_relocate_and_enter` runs where the bootrom loads it. The addresses below are runtime addresses.

| Function | v1.58.2 | v1.88 |
| --- | --- | --- |
| `nboot_relocate_and_enter` | 0x30000000 | 0x30000000 |
| `hang_forever` | 0x30000060 | 0x30000060 |
| `nboot_main` | 0x30E000CC | 0x30E000FC |
| `uart_init` | 0x30E00124 | - |
| `uart_putc` | 0x30E00170 | 0x30E00198 |
| `l2_bind_nf_dma` | 0x30E001EC | 0x30E0020C |
| `l2_copy_from_buf5` | 0x30E0022C | 0x30E0024C |
| `nf_classify_dma_result` | 0x30E002EC | 0x30E0030C |
| `ecc_fix_chunk_from_status` | 0x30E00330 | - |
| `ecc_apply_corrections` | 0x30E00340 | 0x30E00344 |
| `ecc_fix_page_from_hw_regs` | 0x30E00480 | 0x30E0047C |
| `nf_seq_start` | 0x30E004C8 | 0x30E004EC |
| `nf_seq_wait_done` | 0x30E004E8 | 0x30E0050C |
| `nf_emit_addr_cycles` | 0x30E00504 | 0x30E00528 |
| `nf_read_page_with_ecc` | 0x30E00598 | 0x30E005BC |
| `nf_read_1chunk_page` | 0x30E00754 | 0x30E007AC |
| `nf_read_4chunk_page` | 0x30E00774 | 0x30E007CC |
| `nf_read_8chunk_page` | 0x30E00794 | 0x30E007EC |
| `nf_read_raw_range` | 0x30E007B4 | 0x30E0080C |
| `nboot_init_nand_params` | 0x30E008DC | 0x30E00958 |
| `nboot_read_page_or_meta` | 0x30E00994 | 0x30E00A68 |
| `nboot_classify_block` | 0x30E00B50 | 0x30E00C38 |
| `nboot_load_eboot` | 0x30E00C04 | 0x30E00CD4 |

Three rows need a note. `hang_forever` is unreferenced and never runs, thus its address is only its position in the loaded image. v1.88 has no `uart_init`, because the DDR init script that the bootrom runs configures the UART. v1.88 also has no `ecc_fix_chunk_from_status`, because the compiler inlined it.
