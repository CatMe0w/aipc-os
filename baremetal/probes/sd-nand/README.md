# sd-nand

Tests two things that a copy between the SD card and NAND needs: `sd_write_blocks()` of `lib/sd.c`, and a change from the SD card to NAND and back many times in one run.

The probe compiles `lib/sd.c` and `lib/nand.c` into itself. It runs from DDR in USB boot mode with the MMU and the caches off, and it returns to the bootrom.

## Modes

The host writes the mode to the second word of the result block before it starts the probe.

| Mode | What the probe does |
| --- | --- |
| 0 | Reads 1024 blocks from LBA 1024 into DDR at `0x32100000`. It writes nothing |
| 1 | Mode 0, then writes a pattern to the same blocks, reads it back, writes the first contents back, and reads again |
| 2 | Mode 0, then 20 rounds: `sd_init()`, read 64 blocks, `sd_release_pins()`, `nand_init()`, read NAND page 128 with no ECC. It compares each read with the first one |

Every mode stops before the first read of the range if the MBR is not valid, or if a partition starts below LBA 2048. Thus the range is always outside every partition.

## Write safety

Mode 1 writes to the card. Run mode 0 first and save the 524288 bytes at `0x32100000`. If mode 1 stops between its two writes, the card keeps the pattern in LBA 1024 to 2047, and that file is the only copy of the first contents.

## Running

```
make
uv run ak7802-usbboot write --addr 0x32000000 build/sd-nand.bin
uv run ak7802-usbboot poke 0x32008004 MODE
uv run ak7802-usbboot exec --addr 0x32000000 --wait
uv run ak7802-usbboot read --addr 0x32008000 --len 0x80 out.bin
```

Initialize DDR first, see [aipc-ddr-init](../../../tools/aipc-ddr-init/).

## Result layout

Results are 32-bit words from `0x32008000`. A word that the run did not reach holds `0xDEAD`. A return code is `0` on success.

| Word | Contents |
| --- | --- |
| 0 | magic `0x53444E31` |
| 1 | mode |
| 2 | last stage: 3 after mode 0, 6 after mode 1, 9 after mode 2 |
| 3, 22 | return codes of `sd_init()` and `sd_high_speed()` |
| 4, 5 | return code of the MBR read, and the lowest partition start |
| 6 | return code of the first read of the range |
| 7, 8, 9 | mode 1: return codes of the pattern write and of its read back, and the number of words that differ |
| 10, 11, 12 | mode 1: the same three for the write of the first contents |
| 13 | mode 2: return code of the first NAND read |
| 14 | mode 2: rounds done |
| 15, 16, 17 | mode 2: rounds where `sd_init()` failed, where the read failed, and where the data was different |
| 18, 19 | mode 2: rounds where the NAND read failed, and where the page was different |
| 20, 21 | mode 2: return code of a last read of all 1024 blocks, and the number of words that differ |

## Results

On the v1.58.2 device, with the card at 31 MHz:

- Mode 1: both writes of 1024 blocks returned 0, and both reads back had no word that differs.
- Mode 2, run two times: 20 rounds each, and every count of words 15 to 19 was 0. The last read had no word that differs. NAND page 128 from the probe is equal to the same page of a full NAND image.
