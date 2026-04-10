# nboot Boot Flow

## Phase 0: DDR Initialization (executed by bootrom)

Before transferring control to nboot, the AK7802 bootrom executes a register
initialization script embedded in the nboot image header. This script brings up
DDR SDRAM, which nboot itself requires to run.

The script programs SYSCTRL and the DDR controller at `0x2002D000`. The two
firmware versions differ in two places (marked with `*`):

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

**v1.88 difference 1** - `0x20026000 <- 0x30200433` (UART base register): v1.88
initializes the UART clock and baud rate here, during the bootrom-executed DDR
init phase, before the ARM payload starts. As a result, v1.88's `nboot_main` no
longer calls a separate `uart_init()` - the UART is already configured by the
time the first character is printed.

**v1.88 difference 2** - `0x2002D008`: the upper half-word changes from `0x0005`
to `0x0003`. This register controls DDR refresh timing; the change likely reflects
a different DDR chip or revised timing parameters between the two device
generations.

After the script completes, DDR is operational and the bootrom loads the nboot
ARM payload to `0x30000000` and jumps there.

## Phase 1: Self-Relocation (`nboot_relocate_and_enter`, `0x30000000`)

`nboot_relocate_and_enter` is the true ARM entry point. It performs three tasks
before running any other nboot code:

**1. Reset SYSCTRL control registers:**

```
MEMORY[0x0800000C] = 0   # SYSCTRL+0x0C
MEMORY[0x08000034] = 0   # SYSCTRL+0x34
MEMORY[0x08000038] = 0   # SYSCTRL+0x38
```

**2. Copy self to upper DDR:**

Copies the first `0xD00` bytes of itself (the entire active code region) from
`0x30000000` to `0x30E00000`, word by word:

```asm
MOV  R0, #0x30000000      ; source
LDR  R1, =0x30E00000      ; destination
LDR  R3, =0x30E00D00      ; end (exclusive)  [v1.88: 0x30E00DB0]
loop:
    LDRCC R2, [R0], #4
    STRCC R2, [R1], #4
    BCC   loop
```

After relocation, nboot runs entirely from `0x30E00000`, leaving `0x30000000`
free for eboot. The handoff target is `0x30038000`, but nboot actually starts
copying the `IPL` container at `0x30037FD4`, so the `0x2C`-byte `IMG` header
lands immediately before the entry point and the first payload instruction
(`IPL.raw+0x2C`) ends up at `0x30038000`.

**3. Set up CPU mode and stack pointers, then jump:**

```asm
MOV  R0, #0x12            ; IRQ mode
MSR  CPSR_fc, R0
LDR  SP, =0x30FFFF00      ; IRQ stack (top of DDR)

MOV  R0, #0x13            ; SVC mode
MSR  CPSR_fc, R0
LDR  SP, =0x30036000      ; SVC stack

LDR  PC, =0x30E000CC      ; jump to relocated nboot_main  [v1.88: 0x30E000FC]
```

## Phase 2: NAND Initialization (`nboot_main`, `0x30E000CC`)

```c
void __noreturn nboot_main()
{
    uart_init();
    uart_putc('S');           // UART ready, NAND init starting
    nboot_init_nand_params();
    uart_putc('L');           // NAND ready, loading eboot
    nboot_load_eboot(0x30037FD4, /*start_block=*/2, /*max_bytes=*/0x64000);
    uart_putc('B');           // eboot loaded, jumping
    ((void(*)(void))0x30038000)();
    // never returns
}
```

### UART Progress Markers

| Character | Hex  | Meaning                                                               |
| --------- | ---- | --------------------------------------------------------------------- |
| `S`       | 0x53 | UART initialized; NAND parameter init starting                        |
| `L`       | 0x4C | NAND initialized; eboot load starting                                 |
| `B`       | 0x42 | eboot loaded; jumping to eboot                                        |
| `e`       | 0x65 | Bad block skipped (printed per bad block)                             |
| `E`       | 0x45 | Page read error; skipping ahead 2 blocks (v1.58.2) or 1 block (v1.88) |
| `V`       | 0x56 | NAND parameter sanity check failed; nboot halts (v1.88 only)          |

A successful boot with no bad blocks or read errors prints exactly `SLB` on the
UART before handing off to eboot.

### NAND Parameter Initialization

`nboot_init_nand_params` reads a parameter table embedded in the nboot image at
offset `0x64` (at `0x30E00064` after relocation). It extracts NAND geometry and
timing values into runtime variables and programs the NAND controller timing
registers.

The two firmware versions differ significantly here. v1.58.2 reads 9 dwords
(36 bytes) from the table; v1.88 reads 24 dwords (96 bytes) and stores more
parameters. v1.88 also adds a sanity check: if a specific parameter exceeds
`0x20`, it prints `'V'` and halts rather than proceeding with bad values.

**v1.58.2 runtime variables** (stored at `0x30E00D00-0x30E00D13`):

| Address      | Description                     |
| ------------ | ------------------------------- |
| `0x30E00D00` | `pages_per_block`               |
| `0x30E00D04` | `chunk_count` (ECC chunks/page) |
| `0x30E00D08` | `big_page` (1=512B, 4/8=2KB)    |
| `0x30E00D0C` | `page_size` (bytes)             |
| `0x30E00D10` | `oob_size` (spare area bytes)   |

**v1.88 runtime variables** (stored at `0x30E00DB0-0x30E00DFC`, larger table):

| Address      | Description                        |
| ------------ | ---------------------------------- |
| `0x30E00DB0` | `pages_per_block`                  |
| `0x30E00DB4` | (additional geometry param)        |
| `0x30E00DE0` | `chunk_count` (ECC chunks/page)    |
| `0x30E00DE4` | (additional param, sanity-checked) |
| `0x30E00DE8` | (additional param)                 |
| `0x30E00DEC` | (additional param)                 |
| `0x30E00DF0` | (additional param)                 |
| `0x30E00DF4` | `page_size` (bytes)                |
| `0x30E00DF8` | (additional param)                 |
| `0x30E00DFC` | (additional param)                 |

Both versions program the same NAND controller registers:

| Address      | Description                      |
| ------------ | -------------------------------- |
| `0x2002A15C` | NAND controller timing reg A     |
| `0x2002A160` | NAND controller timing reg B     |
| `0x2002B000` | ECC/DMA control (set to 0x10000) |

## Phase 3: eboot Loading (`nboot_load_eboot`)

`nboot_load_eboot(dst=0x30037FD4, start_block=2, max_bytes=0x64000)` loads up
to 400 KB from NAND starting at block 2 into DDR. The fixed start block matches
the current `PTB` `IPL` entry, but nboot does not parse `PTB` at runtime.

The `0x30037FD4` destination is deliberate: it is `0x2C` bytes before the
handoff address `0x30038000`, matching the size of the `IMG` wrapper at the
start of `IPL.raw`. As a result:

- `IPL.raw[0x0000:0x002C]` lands at `0x30037FD4-0x30037FFF`
- `IPL.raw[0x002C]` lands at `0x30038000`
- the bytes actually visible to the jumped-to payload are
  `IPL.raw[0x002C:0x64000]` (equivalently `eboot.nb0[0:0x63FD4]`)

**Algorithm:**

1. For each candidate NAND block (starting at block 2):
   - Call `nboot_classify_block` to determine block status.
   - If the block is bad (`status & 2`), print `'e'` and advance to the next block.
2. Within each good block, read pages sequentially using
   `nboot_read_page_or_meta`. For each successfully read page, subtract
   `page_size` from the remaining byte count.
3. If a page read fails, print `'E'` and skip ahead (v1.58.2: 2 blocks;
   v1.88: 1 block).
4. Stop when the remaining byte count reaches zero (all `max_bytes` loaded).

This means nboot loads only the first `0x64000` bytes of the `IPL` partition
slice, even though the `IMG` header inside `IPL.raw` advertises a larger
`0x80000`-byte region.

**Bad block detection** (`nboot_classify_block`):

- Reads the OOB area of the first page of the block (v1.58.2 uses
  `nf_read_raw_range`; v1.88 uses the same ECC-aware page read path).
- If OOB byte at offset 1 is not `0xFF`, the block is marked bad (return 2).
- Otherwise reads the page metadata. Bits 0 and 1 of the metadata byte encode
  additional flags (returned in the low nibble).

## Phase 4: Jump to eboot

After loading completes, nboot jumps directly to `0x30038000`, which is where
the first payload word from `IPL.raw+0x2C` was placed. No parsing of the `IMG`
header is performed; the header is handled only by the shifted destination
address. nboot does not consult the `PTB` load address and does not set up page
tables or enable the MMU before the handoff. eboot is responsible for all
subsequent hardware initialization.
