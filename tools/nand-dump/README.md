# aipc-nand-dump

Full NAND dump tool for AIPC.

Dumps the entire 512 MB NAND flash including OOB/ECC data in about 15 minutes via USB boot mode. The dump can be used to restore the factory system or as a backup before flashing custom firmware. Keep it safe.

This tool is AIPC-specific (requires DDR init and uses AIPC's NAND header for timing). For a chip-generic AK7802 NAND dump tool, see [`tools/nand-dump-min`](../nand-dump-min).

## Building the stubs

Requires `arm-none-eabi-gcc`.

```
cd stub
make
```

This produces `nand_id.bin` and `nand_copy.bin`.

## Running

```
aipc-nand-dump -o nand.bin
```

The device must be in USB boot mode (DGPIO[2] high at power-on).

Options:

- `-o, --output PATH` -- output file for the raw NAND dump (required)
- `--firmware {1.58.2,1.88}` -- DDR init firmware version (default: 1.88)
- `--pages N` -- override total NAND page count
- `--page-size N` -- override page size in bytes (data only, e.g. 2048)
- `--addr-cycles N` -- override NAND address cycle count
- `--ddr-base ADDR` -- DDR buffer base address (default: 0x30000000)

## How it works

1. Connect to the device and initialize DDR via `aipc-ddr-init`.
2. Upload and execute `nand_id` stub: reads the 8-byte NAND ID and probes page 0 for the "ANYKA382" header to extract factory NFC timing.
3. Auto-detect NAND geometry from the ID bytes (or use manual overrides).
4. Upload `nand_copy` stub once. For each batch:
   - Write parameters (start page, count, DDR address, timing) to L2 SRAM.
   - Execute the stub, which reads pages via the NFC sequencer into DDR.
   - Read the DDR buffer back to the host via `read_mem`.
5. Write the raw dump to file.

Each batch fills ~61 MB of DDR (~31K pages for a 2KB-page NAND). A 512 MB NAND completes in 9 batches.

## Raw dump format

Each physical page is stored as `chunks_per_page` x 528 bytes:

```
[512B data | 16B OOB/ECC] x chunks_per_page
```

For a 2048-byte page NAND: 4 x 528 = 2112 bytes per page.

To extract clean data, strip the OOB from each chunk:

```python
def strip_oob(page_2112: bytes) -> bytes:
    out = bytearray()
    for i in range(4):
        out += page_2112[i * 528 : i * 528 + 512]
    return bytes(out)  # 2048 bytes
```

_TODO: decode the OOB/ECC data and correct errors where possible._

## Memory layout

The stubs run in L2 buffer SRAM (5504 bytes at `0x48000000`). DDR at `0x30000000` is used as a large read buffer after initialization.

| Address range             | Size    | Usage                          |
| ------------------------- | ------- | ------------------------------ |
| `0x48000000 - 0x4800003F` | 64 B    | USB TX staging (HW-managed)    |
| `0x48000040 - 0x4800007F` | 64 B    | Parameter / result block       |
| `0x48000200 - 0x4800023F` | 64 B    | USB RX DMA target (HW-managed) |
| `0x48000240 - 0x48000BFF` | 2.5 KB  | Stub code / rodata / bss       |
| `0x48000C00 - 0x48000E6F` | 624 B   | I/O scratch buffer             |
| `0x48000E70 - 0x48000FFC` | ~400 B  | Bootrom call chain stack       |
| `0x48001100 - 0x4800157B` | ~1.1 KB | Stub stack                     |
| `0x30000000 - 0x33FFFFFF` | 64 MB   | DDR read buffer (63 MB usable) |
