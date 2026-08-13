# nboot Boot Flow

## Phase 0: DDR Initialization (executed by bootrom)

Before it gives control to nboot, the AK7802 bootrom runs a register initialization script from the nboot image header. This script brings up DDR SDRAM, which nboot needs in order to run.

The script programs SYSCTRL and the DDR controller at `0x2002D000`. The two firmware versions differ in two places, marked with `*`:

```
0x080000DC <- 0x00000000   # SYSCTRL reset
0x08000004 <- 0x0000D000   # SYSCTRL clock config
DELAY 200 ticks
0x20026000 <- 0x30200433   # UART clock/baud config  (* v1.88 only)
0x08000064 <- 0x08000000   # SYSCTRL memory config
0x080000A8 <- 0x04000000   # SYSCTRL memory config
0x2002D004 <- 0x0F506B95   # DDR controller timing
DELAY 968 ticks
0x2002D000 <- 0x40170000   # DDR init sequence (NOP, precharge, mode regs, refresh...)
0x2002D000 <- 0x40120400
DELAY 1 tick
0x2002D000 <- 0x40104000
DELAY 1 tick
0x2002D000 <- 0x40100123
DELAY 1 tick
0x2002D000 <- 0x40120400
DELAY 1 tick
0x2002D000 <- 0x40110000
DELAY 1 tick
0x2002D000 <- 0x40110000
DELAY 1 tick
0x2002D000 <- 0x40100023
DELAY 1 tick
0x2002D000 <- 0x60170000   # DDR controller enable
0x2002D008 <- 0x00057C58   # DDR refresh timing  (* v1.88: 0x00037C58)
END
```

**v1.88 difference 1** - `0x20026000 <- 0x30200433`, the UART control register. v1.88 sets the UART clock and baud rate here, inside the DDR init phase that the bootrom runs, before the ARM payload starts. The `nboot_main` of v1.88 therefore no longer calls a separate `uart_init()`. The UART is ready by the time the first character goes out. The `uart_init` of v1.58.2 writes a different value, `0x30200208`, to the same register.

**v1.88 difference 2** - `0x2002D008`. The upper half-word changes from `0x0005` to `0x0003`. This register controls the DDR refresh timing. The change probably reflects a different DDR chip, or revised timing parameters between the two device generations.

The rest of the header tells the bootrom how to read the payload:

| Offset | v1.58.2 value | Meaning |
| --- | --- | --- |
| +0x0C | `04 06 02 02` | chunks_per_page 4, page_count 6, cmd_count 2, dummy_count 2 |
| +0x10 | `03 00 30 00` | 3 address bytes, cmd1 `0x00`, cmd2 `0x30` |
| +0x14 | `0x000F5AD1` | NF timing override 0 (`0x2002A05C`) |
| +0x18 | `0x000F5C5C` | NF timing override 1 (`0x2002A060`) |
| +0x1C | `0005 0005` | pre-delay 5 ticks, sequencer wait 5 ticks |
| +0x20 | `0x00000006` | image type 6 (DDR target) |

The two dummy address words, plus three real address bytes, give the five address cycles that this device needs. `page_count = 6` at four 512-byte chunks per page is exactly `0x3000` bytes, the size of the nboot payload. The two timing values are the same ones that `nand_detect_device` in EBOOT writes.

In this header v1.88 differs only in `page_count`, which is 3 for its smaller `0x1800`-byte payload. Every other field is identical, including the address cycle layout and both timing overrides.

After the script ends, DDR works. The bootrom then loads the nboot ARM payload to `0x30000000` and jumps there.

## The NBT Partition Has No ECC

The bootrom reads nboot with `nf_read_chunk_to_buf`, which programs the DMA control register as `(0x200 << 7) | 0x100018`. `DEC_EN` is clear, thus the BCH engine stays out of the path. The READ command sequence goes out once per page, and four consecutive 512-byte transfers pull from it. The bootrom therefore gets the first 2048 physical bytes of each 2112-byte page, verbatim.

Blocks 0 and 1 match that. The image occupies the first 2048 bytes of each physical page contiguously, with no per-chunk spare and no parity anywhere. A raw dump shows this directly. Physical columns `0x200` and above of pages 1 and 2 hold ARM instructions rather than spare data, and `page[:2048]` reproduces `nboot.akimg` exactly, where a 528-byte de-interleave does not.

nboot is therefore stored with no protection. A single bit flip anywhere in block 0 corrupts it, and neither the bootrom nor anything else can detect or repair it. Every other partition on the device, `IPL` included, uses the ECC-protected 528-byte chunk layout in [docs/eboot/nand-driver.md](../eboot/nand-driver.md).

## Phase 1: Self-Relocation (`nboot_relocate_and_enter`)

`nboot_relocate_and_enter` is the true ARM entry point. It does three things before any other nboot code runs.

**1. Reset the SYSCTRL control registers:**

```
MEMORY[0x0800000C] = 0   # module clock gates and software resets
MEMORY[0x08000034] = 0   # module IRQ mask
MEMORY[0x08000038] = 0   # module FIQ mask
```

A clear of `+0x0C` turns on every module clock and releases every module reset. This undoes the selective gating that the bootrom applied at entry. A clear of `+0x34` and `+0x38` masks all module interrupt sources. Both readings follow the register semantics in [docs/bootrom/memory-map.md](../bootrom/memory-map.md), which also notes that the interrupt mask polarity has two readings.

**2. Copy itself to upper DDR:**

The stub copies the first `0xD00` bytes (v1.58.2) or `0xDB0` bytes (v1.88) of itself, the whole active code region, from `0x30000000` to `0x30E00000`, word by word:

```asm
MOV  R0, #0x30000000      ; source
LDR  R1, =0x30E00000      ; destination
LDR  R3, =0x30E00D00      ; end (exclusive)  [v1.88: 0x30E00DB0]
loop:
    LDRCC R2, [R0], #4
    STRCC R2, [R1], #4
    BCC   loop
```

The copy length and the base of the runtime variables are the same constant. The variables start exactly where the copied image ends.

After the relocation, nboot runs entirely from `0x30E00000` and leaves `0x30000000` free for EBOOT. The handoff target is `0x30038000`, but nboot starts the copy of the `IPL` container at `0x30037FD4`. The `0x2C`-byte `IMG` header therefore lands immediately before the entry point, and the first payload instruction (`IPL.raw+0x2C`) lands at `0x30038000`.

**3. Set up the CPU mode and the stack pointers, then jump:**

```asm
MOV  R0, #0x12            ; IRQ mode
MSR  CPSR_fc, R0
LDR  SP, =0x30FFFF00      ; IRQ stack (top of DDR)

MOV  R0, #0x13            ; SVC mode
MSR  CPSR_fc, R0
LDR  SP, =0x30036000      ; SVC stack

LDR  PC, =0x30E000CC      ; jump to relocated nboot_main  [v1.88: 0x30E000FC]
```

nboot sets up only these two stacks. It installs no exception vectors, leaves the MMU and the caches alone, and touches no other CPU state. Both mode writes leave the CPSR I and F bits clear, thus the CPU accepts IRQ and FIQ for the whole life of nboot, with the bootrom vector table still in place. Nothing can assert, because the two mask registers went to zero first.

## Phase 2: NAND Initialization (`nboot_main`)

```c
void __noreturn nboot_main()
{
    uart_init();
    uart_putc('S');           // UART ready, NAND init starting
    nboot_init_nand_params();
    uart_putc('L');           // NAND ready, loading EBOOT
    nboot_load_eboot(0x30037FD4, /*start_block=*/2, /*max_bytes=*/0x64000);
    uart_putc('B');           // EBOOT loaded, jumping
    ((void(*)(void))0x30038000)();
    // never returns
}
```

Every parameter is a compile-time constant. nboot does not read the partition table, does not look at the `IMG` header inside the container that it loads, and runs no magic, length or checksum validation of any kind.

### UART Progress Markers

| Character | Hex | Meaning |
| --- | --- | --- |
| `S` | 0x53 | UART initialized; NAND parameter init starting |
| `L` | 0x4C | NAND initialized; EBOOT load starting |
| `B` | 0x42 | EBOOT loaded; jumping to EBOOT |
| `e` | 0x65 | Bad block skipped (printed per bad block) |
| `E` | 0x45 | Page read error; skipping ahead 2 blocks |
| `V` | 0x56 | NAND parameter sanity check failed (v1.88 only); nboot continues anyway |
| `d` | 0x64 | Tag scratch would overflow on a small-page device (v1.88 only); unreachable on this hardware |

A boot with no bad blocks and no read errors prints exactly `SLB` on the UART before the handoff to EBOOT.

`uart_init` enables the UART sharepin (`SYSCTRL+0x78` bit 9), sets bit 11 of `0x2002C040`, writes `0x30200208` to the UART control register, and clears `0x2002600C`. `uart_putc` pushes each character through the L2 UART port. It sets bit 16 of `0x2002C04C`, stores the character to `L2_UART_TX_PORT` (`0x48001000`), and clears `L2_UART_TX_FRAC_PORT` (`0x4800103C`). It then starts the transfer through UART+0x00 bit 28 and UART+0x04 bits 4 and 16, and spins until the remaining-count field in UART+0x08 reaches zero.

### NAND Parameter Initialization

`nboot_init_nand_params` reads a parameter table in the nboot image at offset `0x64`, which is `0x30E00064` after the relocation. v1.58.2 reads 9 dwords, 36 bytes, into a stack copy and keeps five of the fields:

| Table offset | Size | v1.58.2 value | Runtime variable | Meaning |
| --- | --- | --- | --- | --- |
| +0x00 | 4 | `0x9510DCAD` | - | magic, never checked |
| +0x04 | 2 | `0x0800` | `0x30E00D0C` | `page_size` in bytes |
| +0x06 | 2 | `0x0040` | `0x30E00D00` | `pages_per_block` |
| +0x0F | 1 | `0x02` | `0x30E00D10` | column (in-page) address cycle count |
| +0x11 | 1 | `0x03` | `0x30E00D04` | row (page) address cycle count |
| +0x18 | 4 | `0x00030230` | `0x2002A15C` | NAND controller timing reg A |
| +0x1C | 4 | `0x00040203` | `0x2002A160` | NAND controller timing reg B |

`0x30E00D08` comes from a calculation, not from the table. It holds `page_size >> 9`, the number of 512-byte ECC chunks per page, which is 1, 4 or 8 for pages of 512, 2048 and 4096 bytes. On this device it is 4.

`nf_emit_addr_cycles` consumes the two address-cycle counts. It emits `[0x30E00D10]` bytes of the column address, then `[0x30E00D04]` bytes of the row address. Two plus three cycles matches a part with 4096 blocks of 64 pages of 2048+64 bytes.

The routine then writes `0x10000`, `NFC_EN` alone, to the DMA control register at `0x2002B000`.

The v1.88 build reads 23 dwords, 92 bytes, instead of 9, into a different set of runtime variables. It also moves four values out of the code and into the table:

| Table offset | Size | v1.88 value | Runtime variable | Meaning |
| --- | --- | --- | --- | --- |
| +0x00 | 4 | `1` | `0x30E00DF0` | page mode, 0/1/2 = 512/2048/4096-byte pages |
| +0x08 | 4 | `0x0800` | `0x30E00DF4` | `page_size` in bytes |
| +0x0C | 4 | `0x10` | `0x30E00DE4` | sanity-checked against `0x20` |
| +0x10 | 4 | `0x80` | `0x30E00DB0` | `pages_per_block` |
| +0x20 | 4 | `3` | `0x30E00DB4` | row address cycle count |
| +0x24 | 4 | `2` | `0x30E00DFC` | column address cycle count |
| +0x30 | 4 | `0xA514DCAD` | - | magic, never checked |
| +0x40 | 4 | `0x000F5AD1` | `0x2002A15C` | NAND controller timing reg A |
| +0x44 | 4 | `0x000F5C5C` | `0x2002A160` | NAND controller timing reg B |
| +0x48 | 1 | `0` | `0x30E00DE8` | ECC mode, 0 = 4-bit BCH, 1 = 8-bit |
| +0x4C | 4 | `4` | `0x30E00DE0` | number of ECC error-position registers |
| +0x54 | 4 | `4` | `0x30E00DF8` | tag bytes per chunk |
| +0x58 | 4 | `7` | `0x30E00DEC` | ECC parity bytes per chunk |

The page mode replaces the chunk count that v1.58.2 derives from `page_size >> 9`, and it matches the `0/1/2` convention of the EBOOT chip database. The last four fields let v1.88 assemble the DMA control word at run time as `(ecc_mode << 22) | ((528 - ecc_bytes) << 7) | 0x0C10001A`. For mode 0 and seven parity bytes this is exactly the `0x0C11049A` that v1.58.2 writes as a constant.

The sanity check on `+0x0C` prints `'V'` when the value exceeds `0x20`, but it does **not** halt. Execution falls through to the timing writes, and the boot continues.

## Phase 3: EBOOT Loading (`nboot_load_eboot`)

`nboot_load_eboot(dst=0x30037FD4, start_block=2, max_bytes=0x64000)` loads the `IPL` partition from NAND into DDR, from block 2 on. The fixed start block matches the current `PTB` `IPL` entry, but nboot does not parse `PTB` at runtime.

The `0x30037FD4` destination is deliberate. It is `0x2C` bytes before the handoff address `0x30038000`, the size of the `IMG` wrapper at the start of `IPL.raw`. The result:

- `IPL.raw[0x0000:0x002C]` lands at `0x30037FD4-0x30037FFF`
- `IPL.raw[0x002C]` lands at `0x30038000`
- the payload sees `IPL.raw[0x002C:0x64000]`, or `eboot.nb0[0:0x63FD4]`

**Algorithm:**

1. Classify the current block. While the result has bit 1 set, print `'e'` and advance one block.
2. Inside a good block, read pages in order into `dst + (max_bytes - remaining)` with `nboot_read_page_or_meta`.
3. If a page read fails, print `'E'`, skip ahead two blocks, and restart at step 1.
4. After each successful page, stop if the remaining byte count was already below `page_size`. Otherwise subtract `page_size` and continue. After `pages_per_block` pages, advance to the next block and return to step 1.

The termination test in step 4 compares the remaining count _before_ it decrements the count, thus the loop always reads one page more than the byte budget calls for. With `max_bytes = 0x64000` and a 2048-byte page, the 200th page brings the remaining count to exactly zero. A 201st page still reads and writes at `dst + 0x64000`. nboot therefore writes `0x64800` bytes and ends at `0x3009C7D3`, not at `0x3009BFD3`. Anything in that trailing 2 KB is destroyed.

The block index has no upper bound. If every remaining block were bad, the skip loop would scan past the end of the device forever.

**Bad block detection** (`nboot_classify_block`):

The first page of the block takes a raw, ECC-disabled 4-byte read at physical column `0x410`, which is `528 * 1 + 512`, the start of the spare area of chunk 1. The column is a physical offset into the 2112-byte page, even with the ECC engine off. Hardware confirms this: a 512-byte read at column `0x200` of an ECC-formatted page returns the spare area of chunk 0, not data chunk 1. If the read fails, or if the byte at column `0x411` is not `0xFF`, the block is bad and the function returns 2. On a small-page part the probe goes to page 1 at column `0x200` instead.

Otherwise the function fetches the eight metadata bytes, four bytes each from columns `0x410` and `0x200`, and decodes the byte at column `0x410` as a flags byte. A clear bit 1 gives status 4, and a clear bit 0 adds 8. With bit 1 set, the return value keeps the column constant instead, thus the result is not always a small status code. Callers test only bit 1, and both `0x200` and `0x410` read as clear there.

On the v1.58.2 device the bad block marker is `0x00` for exactly one block, 3264 out of 4096. The flags byte reads `0xFF` for block 0, `0xFE` for blocks 1 through 35, and `0xFD` from block 36 on. Block 36 is the start of the `NK` partition. The factory flashing tool writes these markers.

**Page reads** (`nboot_read_page_or_meta`, `nf_read_page_with_ecc`):

For a 2 KB page, nboot issues the READ command sequence once per page: `cmd 0x00`, five address cycles, `cmd 0x30`, wait. It then loops over the four chunks. For each chunk it re-arms only the DMA descriptor and a single transfer micro-op:

```
0x2002B000 = 0x0C11049A   # BYTE_CFG 521, NFC_EN, 4-bit BCH, ADDR_CLR, START, DEC_EN
0x2002A100 = 0x00107919   # ((528 - 1) << 11) | 0x119, transfer 528 bytes
```

Each chunk therefore pulls 528 physical bytes through the sequencer. The BCH engine consumes 521 of them for the correction and delivers 512 clean data bytes into the L2 buffer. `l2_copy_from_buf5` (0x30E0022C) then copies them to `dst + chunk * 512`. Four chunks walk exactly one 2112-byte physical page, which is why the column address never advances between chunks.

Hardware reproduces this. Against a page whose four data chunks all differ, the four blocks each match the 528-byte stride over the full 512 bytes, and together they reconstruct the ECC-stripped logical page exactly. A 512-byte stride matches only the first chunk.

After each chunk, nboot waits for `END` (bit 6) and `DEC_RDY` (bit 24) in the DMA control register, clears `END`, and classifies the result. Bit 26 (`NO_ERR`) means clean. Bit 27 (`RESULT_NO_OK`) means uncorrectable. Neither bit set means that correctable errors are pending. Hardware shows all three, as `0x059104C2`, `0x099104C2` and `0x019104C2`.

The BCH engine does not correct in place. On a correctable chunk the 512 bytes that it delivers to the L2 buffer still contain the flipped bit, thus the software correction path below is load-bearing, not a fallback.

**Software ECC correction** (`ecc_fix_page_from_hw_regs`):

When a correction is pending, nboot reads four error-position registers, one per correctable bit at t=4. Each register carries a 10-bit segment mask in bits [18:9] and a 9-bit position in bits [8:0].

The array starts at `0x2002B004`, which is where the engine writes on hardware, and it leaves the rest of the block at zero. v1.88 reads from there. **v1.58.2 reads from `0x2002B008` instead**, thus it sees zero where the position went, and it reads one register past the end. `ecc_apply_corrections` returns at once on a zero mask, the loop then reports success, and the chunk goes on uncorrected. v1.58.2 therefore corrects no single-bit error at all. It passes damaged data to EBOOT without a message.

The segment mask selects one of five 128-byte segments of the 528-byte chunk, through the base offsets -112, 16, 144, 272 and 400 bytes. The odd member of each bit pair adds one to the position. The bit offset inside the segment is `2 * position + parity - 4`, and it borrows from the previous byte when that value goes negative. Bits are numbered MSB-first inside each byte.

The borrow is cosmetic. It cancels out, and the whole rule reduces to one expression for the bit offset from the start of the chunk, counting MSB first:

```
offset = 8 * base + 2 * position + parity - 4
```

This also inverts directly, and gives the register that the engine would report for any given bit.

Hardware confirms the rule in both directions, across four different segment selectors. The register that the engine reports for a marginal cell matches the value predicted from a software BCH decode, and the inverse predicts the register from a decoded bit position. Note that a software decoder usually numbers bits LSB first inside a byte, where the engine numbers them MSB first, thus `byte = position >> 3` and `mask = 1 << (position & 7)` relate the two conventions. On an uncorrectable chunk the engine still writes whatever its search produced, and those positions can decode out of range. Offsets up to `0x1FF` flip in the data chunk, and offsets `0x200` through `0x208` flip in the four-byte tag scratch of the caller. Anything larger goes away. An unknown segment mask aborts the whole read.

On an uncorrectable result, nboot checks whether the chunk is simply blank. All 512 data bytes must be `0xFF`, and so must nine further bytes. That second buffer is an uninitialized stack slot, not the spare area of the chunk, thus the check does not do what it appears to do. Only a genuine uncorrectable error reaches this path.

**L2 buffer binding** (`l2_bind_nf_dma`):

```
0x2002C084 |= 0x30000000        # DMA path config, bits [29:28]
0x2002C088 |= 0x00200000        # enable, bit 21
0x2002C090  = (old & ~0xE00) | 0xA00   # bits [11:9] = 5
0x2002C088 |= 0x20000000        # flush, bit 29
```

Four independent facts agree that the NF path uses L2 buffer 5: the assignment field in `0x2002C090`, the enable and flush bits at 16+5 and 24+5 in `0x2002C088`, the four-bit fill counter at bits [23:20] of `0x2002C0A0` that `l2_copy_from_buf5` polls, and the address that it drains from, `0x48000A00`, which is `0x48000000 + 5 * 512`. See [docs/bootrom/memory-map.md](../bootrom/memory-map.md) for the buffer layout. The NAND path of the bootrom uses buffer 0 at `0x48000000` instead.

## Phase 4: Jump to EBOOT

After the load, nboot jumps directly to `0x30038000`, where the first payload word from `IPL.raw+0x2C` went. It parses no `IMG` header. The shifted destination address alone handles the header. nboot does not consult the `PTB` load address, and it does not set up page tables or enable the MMU before the handoff. EBOOT does all the hardware initialization from there on.

## Version Differences

The two builds are the same program with a different compile-time configuration, not two different loaders. Every function in v1.58.2 has a counterpart in v1.88, with two exceptions. `uart_init` is gone, because the header script that the bootrom runs now configures the UART, and `ecc_fix_chunk_from_status` is inlined. All of the following are identical: the relocation stub, every address constant in `nboot_main`, the L2 binding and drain sequences, the sequencer start value, the ECC correction algorithm with its five segment base offsets, the one-block bad-block skip, the two-block read-error skip, and the one-page overshoot at the end of the load loop.

What changed:

**Device geometry.** The two devices carry different NAND parts. v1.58.2 reports 64 pages per block over 4096 blocks. v1.88 reports 128 pages per block over 2048 blocks. Both come to 512 MB. The runtime NAND timing values differ with them: `0x00030230` and `0x00040203` against `0x000F5AD1` and `0x000F5C5C`.

**Parameterization.** The ECC mode, the parity byte count, the tag byte count and the error-position register count move out of the code and into the parameter table, as the table above shows. An explicit page mode field replaces the chunk-count derivation.

**Tag bytes are now read.** The ECC page read of v1.58.2 takes a tag scratch pointer but never writes to it. Only the software correction path touches that buffer. v1.88 drains `0x30E00DF8` tag bytes from the L2 buffer after the 512 data bytes of each chunk. This makes the blank-page check meaningful: v1.58.2 compares nine bytes of an uninitialized stack slot against `0xFF`, where v1.88 compares the tag bytes that it read.

**Correction bounds.** v1.58.2 writes corrections at chunk offsets `0x200` through `0x208`, nine bytes, into a buffer with only four bytes per chunk. v1.88 bounds the same writes by `512 + tag_bytes`. It also applies the `-4` bit-position bias only in 4-bit BCH mode, where v1.58.2 applies it in every mode.

**Error-position register base.** v1.58.2 reads four registers from `0x2002B008`. v1.88 reads `0x30E00DE0` registers from `0x2002B004`. Hardware writes the array at `0x2002B004`, thus v1.88 is correct and v1.58.2 corrects no recoverable bit error, as Phase 3 describes. This is the one difference between the two builds with a functional consequence.

**Block classification.** v1.58.2 probes the bad block marker with a direct raw read at physical column `0x410`. v1.88 goes through the metadata path instead, which walks the chunk spare areas from column 512 in steps of 528, and takes `min(remaining, tag_bytes)` per step. With four tag bytes this gives the same two reads at columns `0x200` and `0x410`, thus the marker still lands at physical column `0x411`. v1.88 also returns a clean `0`, `4`, `8` or `12`, where v1.58.2 can leave the column constant in the returned value.

**Small-page paths.** v1.88 drops the third small-page command case of v1.58.2, the `CMD 0x50` spare read for a column that reaches `0x200`. It also folds the small-page special case of v1.58.2, which probes page 1 instead of page 0, into the metadata path. Neither path is reachable on either device, because both report large pages.

## Unresolved

- The `0x40008400` that nboot writes to `NF_SEQ_CTRL_STA` is only partly decoded. Bit 30 starts the sequence and bit 10 selects chip 0, which matches the established start value. That established value sets bit 9 where nboot sets bit 15, and bit 15 lies in a write-protect field. The bootrom writes `0x40000600`, which is exactly the established start value for chip 0. The reason for the difference in nboot is unknown. [docs/bootrom/memory-map.md](../bootrom/memory-map.md) decodes the micro-op encoding itself.
- Bit 19 of the DMA control register reads back set after an ECC-decoded chunk, even though it went out clear, thus the `BYTE_CFG` field does not read back as written. Whether bit 19 belongs to `BYTE_CFG` at all is unclear.
