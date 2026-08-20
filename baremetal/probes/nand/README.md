# AK7802 NAND read/write

This suite pins down how the NAND controller addresses a page, how its BCH engine reports errors, and how the erase/program interface works. Each stub replays one of the paths found in nboot verbatim, so a mismatch indicates an error in the reverse engineering.

## Running

```
make -C baremetal/probes/nand/stub PAGE=N [ITERS=M]
uv run ak7802-usbboot write --addr 0x48000240 PROBE.bin
uv run ak7802-usbboot exec --addr 0x48000240 --wait
uv run ak7802-usbboot read --addr 0x48000600 --len 0x800 out.bin
```

`PAGE` is compiled in, so run `make clean` when changing it; the Makefile cannot see the change on its own and will happily reuse a stale object. `ITERS` bounds the retry stub only.

For the column stubs, `PAGE` must lie in an ECC-formatted partition, and its four data chunks should all differ, otherwise the two candidate strides produce the same bytes. For the ECC stubs it must be a page with a known bit error; see below for how to find one.

The read stubs and write mode 0 (status only) are non-destructive. All stubs return to the bootrom, so runs can be repeated without a power cycle. Every wait loop is bounded and reports a timeout through the status word instead of hanging.

## Result layout

Results are 32-bit words from `0x48000600`. Word 0 is the magic `0x4E464301` and word 3 is the status: `0` on completion, `0xDEAD` if the stub never reached the end, `0x1nn` if the retry stub stopped on chunk `nn`, otherwise a stage code identifying the wait loop that timed out.

| Word | Contents |
| --- | --- |
| 1, 2 | probe mode and page number |
| 4..6 | column addresses, column modes only |
| 7 | `NF_SEQ_CTRL_STA` at exit |
| 8..11 | DMA control register after each read, column modes |
| 12..15 | checksum over all 512 bytes of each read, column modes |
| 16..19 | per-chunk ECC verdict, ECC modes: 1 clean, 2 correctable, 3 uncorrectable |
| 20..23 | per-chunk checksum of the delivered 512 bytes, ECC modes |
| 24..27 | per-chunk DMA control register, ECC modes |
| 28 | last completed iteration, retry mode |

The column modes store the first 128 bytes of each read at `0x48000C00`, four blocks of 128; only a prefix is retained because L2 SRAM has no room for four full results, and the checksums cover the remainder. The ECC modes instead store a snapshot of `0x2002B000` through `0x2002B01C` per chunk at `0x48000680`, eight words each, taken before the transfer-done bit is cleared.

## Probe sources

`nf_read.c` builds all four read stubs from `PROBE_MODE`, and `nf_write.c` builds three write stubs the same way.

Mode 0 reproduces `nf_read_raw_range`, issuing three ECC-disabled 512-byte reads at columns `0`, `0x200` and `0x400`. Mode 1 reproduces `nf_read_page_with_ecc`, issuing one READ command sequence and then four ECC-decoded chunk transfers. Mode 2 is mode 1 with the ECC block snapshotted per chunk instead of corrections being applied, so the delivered data stays as the engine handed it over. Mode 3 wraps mode 2 in a retry loop that stops on the first chunk the engine does not call clean, which is how a marginal cell gets caught.

Hardware bring-up calls the bootrom's `nf_boot_hw_init`, then applies the timing values and DMA control word nboot writes on entry, then issues a NAND reset. USB boot mode never touches the NAND, so this reproduces the state nboot inherits.

## Write stubs

`nf_write.c` covers block erase and flat page program, the two primitives a replacement first-stage loader needs. Mode 0 reads chip status only and is non-destructive; run it first, because it answers whether the part is write protected before anything is erased. Mode 1 erases the target block and checks it reads back blank. Mode 2 does that and then programs one page with a word pattern and reads it back through the existing raw read.

The target defaults to `NBT` block 1, which the bootrom never reads and which both dev devices (1.58.2 and 1.88) carry erased. Building for any other block requires `ALLOW_ANY_BLOCK`. Erasing block 1 does destroy the factory flags byte its first page carries at physical column `0x410`; nothing in the boot path reads it.

These stubs are larger than the read ones and use `linker_write.ld`, which gives the code `0x48000240` through `0x480009FF`, puts the read scratch at `0x48000C00` and the result header at `0x48001100`. They still run from L2 and return to the bootrom, so a run costs no power cycle.

```
make -C baremetal/probes/nand/stub BLOCK=1 PPB=64
uv run ak7802-usbboot write nf_wr_status.bin --addr 0x48000240
uv run ak7802-usbboot exec --addr 0x48000240 --wait
uv run ak7802-usbboot read --addr 0x48001100 --len 0x50 out.bin
```

Result words: 0 magic `0x4E465701`, 1 mode, 2 block, 3 status (`0` on success), 4 row address, 5 chip status, 6 erase status, 9 program status, 12 first mismatching word index, 15 mismatch count, 16 through 19 the first four words read back.

Confirmed on hardware: chip status `0xE0`, meaning ready and not write protected; erase leaves the block blank; a flat page program of 2048 bytes reads back byte for byte. The write DMA descriptor differs from the read one by the `DIR_WRITE` bit alone, `0x100018` against `0x10001C`, and the L2 common buffer needs its direction bit set for the transfer, bits [15:8] of `L2CTR_BUF0_7_CFG`, which the read path never touches. A page is programmed as one command followed by four 512-byte transfers, mirroring the ECC read path, so the column address never advances.

## Finding a page with a bit error

The write stub programs through the ECC engine, so it cannot construct a page with a known raw bit error. Natural candidates have to be found instead. Decoding a raw dump chunk by chunk with `bchlib.BCH(4, 0x201b)` over `data[0:512] + oob[0:9]` against `oob[9:16]` locates the candidates. On the dump used here, six chunks decoded to exactly one bit error each.

Those cells are marginal rather than dead, so a single read often comes back clean; the retry stub caught each of them within two reads. Erased chunks and chunks written with all-`0xFF` parity are both uncorrectable and cannot be used for a positive test.

## Confirmed boundaries

The column address is a physical offset into the 2112-byte page in every mode. A 512-byte read at column `0x200` returned chunk 0's spare area, matching the raw page over its full length and disagreeing with the interleave-skipping interpretation. Column `0x400` likewise landed 16 bytes before chunk 1's spare area. There is no logical-to-physical translation in the controller.

An ECC-decoded transfer advances 528 bytes. All four chunks matched a 528-byte stride over their full length and concatenated into the ECC-stripped 2048-byte logical page; a 512-byte stride matched only the first chunk.

The DMA control register reports three distinct ECC outcomes, all observed:

| Value | Meaning | Bits |
| --- | --- | --- |
| `0x059104C2` | clean | bit 26 `NO_ERR` set |
| `0x019104C2` | correctable | neither bit 26 nor bit 27 |
| `0x099104C2` | uncorrectable | bit 27 `RESULT_NO_OK` set |

The engine does not correct in place. On a correctable chunk the 512 bytes delivered to L2 still contain the flipped bit, so the software correction path is load-bearing rather than a fallback.

Error positions are written starting at `0x2002B004`, one register per position, with the remainder of the block left at zero. Four marginal chunks were captured live and each reported exactly one position, matching values predicted offline from the `bchlib` error location: `0x000041FB`, `0x0000118A`, `0x00008167` and `0x00010019`. Those cover four different segment selectors, which confirms both the register base and the whole decoding rule, namely that bits [18:9] select one of five 128-byte segments at base offsets -112, 16, 144, 272 and 400, bits [8:0] give a position, the bit offset within the segment is `2 * pos + parity - 4`, and bits are numbered MSB first.

On an uncorrectable chunk the engine still writes whatever its search produced. A chunk with all-`0xFF` parity reported two positions, one of which decodes out of range.

The NF DMA delivers into L2 buffer 5 at `0x48000A00`, not into buffer 0 at `0x48000000` as the bootrom's own NAND path does. Reading the wrong base returns uninitialized SRAM that is identical across successive reads, which is the failure signature to watch for.

Byte stores into L2 SRAM replicate the byte across the whole aligned word, so results must be staged with word stores. A byte-wise copy of a captured block silently keeps only every fourth source byte.

Neither the read nor the write stubs exercise small-page devices or 8-bit BCH mode.
