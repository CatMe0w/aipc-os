# aipc-boot IPL

A custom Initial Program Loader that replaces the OEM EBOOT in the IPL NAND partition.

The IPL boots ARM executables from an SD card when one is available, and otherwise falls back to the original WinCE chain so the device remains dual-bootable.

## Boot flow

1. nboot loads the IPL partition payload to `0x30038000` and jumps to it. (The IMG header sits at `0x30037FD4`; nboot reads `0x64000` bytes.)
2. `start.S` is position independent. It copies the whole payload to the link address `0x32000000`, zeroes BSS, sets the stack, and calls `ipl_main`. Relocating away from `0x30038000` frees that region for the BAK fallback load.
3. `ipl_main` tries the SD path:
   - `sd_init` brings up the MCI controller, runs the SD 2.0 init sequence, switches to 4-bit bus and a fast transfer clock, and binds an L2 DMA buffer.
   - `fat_load_file` locates `BOOT.BIN` in the root directory of the first FAT16/FAT32 partition and loads it to `0x33000000`.
   - On success the IPL jumps to `0x33000000`.
4. If any SD step fails (no card, no FAT partition, no `BOOT.BIN`, read error), the IPL reads the NAND BAK partition into `0x30037FD4` and jumps to EBOOT at `0x30038000`, which boots WinCE.

## Memory map

| Region | Address | Notes |
| --- | --- | --- |
| DDR | `0x30000000` - `0x33FFFFFF` | 64 MB total |
| BAK handoff payload | `0x30038000` | EBOOT entry; IMG header at `0x30037FD4`, SP `0x30036000` |
| IPL log buffer | `0x31D00000` | 64 KB, see Diagnostics |
| IPL code/data | `0x32000000` | link address, 1 MB window, stack top `0x32100000` |
| SD boot payload | `0x33000000` | `BOOT.BIN` destination, 10 MB cap |

The IPL payload must fit the `0x64000`-byte window nboot reads.

## SD boot contract

The boot image must be named `BOOT.BIN` and placed in the root directory of the first FAT16 or FAT32 partition on the card.

`BOOT.BIN` is loaded to `0x33000000` and must not exceed 10 MB. The IPL hands off bare and the CPU stays in SVC mode with caches and MMU off.

## NAND BAK fallback

The BAK partition holds a copy of EBOOT identical to the IPL partition's EBOOT once the IMG wrapper is stripped. Its coordinates are firmware specific: for v1.58.2 the block size is 128 KB and the partition table places BAK at block 18.

`nand.c` is a port of nboot's ECC-aware NAND read path. The runtime NAND geometry it depends on lives at `0x30E00D00`. On the production path nboot populates it; on the dev path `usbboot_run.py` does.

Software ECC correction is not yet ported. A correctable-error chunk is reported as a hard read failure, which on healthy NAND never happens.

## Diagnostics

The UART hardware on our dev device is dead. All diagnostic output goes to a 64 KB memory buffer at `0x31D00000`, same as DOOM. After the IPL halts or hands off, the buffer can be recovered with a cold-boot RAM dump before DDR contents decay. WinCE overwrites the buffer once it boots, so the SD path and the BAK path cannot both be inspected from a single run.

## Building

```
make
```

Requires `arm-none-eabi-gcc`. The build produces `ipl.bin`, the raw payload.

## Dev workflow

Iteration happens entirely over USB boot mode, with no NAND writes. Run the tools from the repo root.

| Tool | Purpose |
| --- | --- |
| `tools/usbboot_run.py` | Init DDR, replicate nboot's NAND init, upload `ipl.bin` to `0x30038000`, and execute it. Bypasses nboot entirely. |
| `tools/dump_log.py` | Cold-boot dump of the log buffer at `0x31D00000`. |
| `tools/wrap_img.py` | Prepend the 44-byte IMG header to `ipl.bin`, producing `ipl.img` for NAND flashing. |

Example:

```
uv run tools/usbboot_run.py --firmware 1.58.2
uv run tools/dump_log.py --firmware 1.58.2
```

## Flashing

`wrap_img.py` produces `ipl.img` with the IMG header the IPL partition expects. The IPL is expected to be written to NAND once per device, at delivery; NAND on these aged devices has visible decay and writes are treated as expensive.

TODO: Currently no custom NAND write tool is provided.
