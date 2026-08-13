# Partition Format

EBOOT keeps a partition table blob, the `PTB`, in RAM at `0x80106EA0`, and it saves snapshots of the blob in the `CFG` partition. The v1.88 EBOOT here uses a fixed table offset. It does not search for entry tags and it does not parse alternate header variants. A separate v1.58.2 NAND dump has the same entry record format and the same tag order, but it stores the table at a different offset.

This document describes:

1. The way EBOOT stores and reloads the `PTB`
2. The raw `PTB` record layout
3. The default partition layout that EBOOT builds
4. The `ECEC`-headed kernel image that the `NK` path expects

## PTB Storage

EBOOT treats the `PTB` as a `0x7F4`-byte payload.

- `sub_80065118` reloads it. It starts at the **last CFG block** and scans the sectors **forward inside that block**. It then moves to the previous CFG block and repeats. It skips bad blocks and remembers the last sector whose first word is `PTB\0`.
- `sub_80066958` saves it. It writes the current `0x7F4`-byte snapshot into successive sectors from sector `0` of the **last CFG block**, advances inside the block, then moves to the previous block. When the partition runs out of clean slots, EBOOT reformats `CFG` and starts again from the last block.

In the stock layout that EBOOT builds, `CFG` occupies the three blocks immediately before the four-block `END` tail entry. The `PTB` header also records the current saved sector number.

### PTB Header

| Offset | Size | Field | Meaning / default |
| --- | --- | --- | --- |
| `+0x00` | 4 | magic | `"PTB\0"` |
| `+0x04` | 4 | version | `"01\0\0"` |
| `+0x08` | 4 | `save_sector` | sector number of the last saved snapshot; default `0xFFFFFFFF` |
| `+0x0C` | 4 | `save_count` | incremented by `sub_80066958`; default `0` |
| `+0x10` | 4 | IP address | default `0x0B00A8C0` (LE `192.168.0.11`) |
| `+0x14` | 4 | subnet mask | default `0x00FFFFFF` (LE `255.255.255.0`) |
| `+0x18` | 4 | reserved | default `0` |
| `+0x1C` | 2 | reserved | default `0` |
| `+0x1E` | 1 | boot delay | default `0`; in NAND only, because the base-options menu is unreachable, see boot-flow.md |
| `+0x1F` | 1 | reserved | default `0` |
| `+0x20` | 4 | boot flags | default `0`; bit `1` is the DHCP toggle |
| `+0x24` | 4 | default boot target | default `4` (`NK`); the menu also uses `9` = menu, `10` = KITL |
| `+0x28` | 4 | KITL transport | default `0` (`AKUSB`); `1` selects `ENC28J60` |
| `+0x2C..+0x66F` | .. | opaque region | the builder preserves it; the current EBOOT analysis does not decode it |

`ptb_load_default_network_config` writes the factory-default values for `+0x10..+0x28`. The base-options menu edits the same in-RAM fields, and `Save Change` persists them through the PTB save path above.

## PTB Entry Layout

In the v1.88 EBOOT, the entry table starts at the fixed offset `0x670`. It holds eight raw records, each `0x30` bytes wide.

| Raw Offset | Size | Field                            |
| ---------- | ---- | -------------------------------- |
| `+0x00`    | 4    | opaque word                      |
| `+0x04`    | 4    | tag (ASCII, NUL-padded)          |
| `+0x08`    | 16   | filename (ASCII, NUL-terminated) |
| `+0x18`    | 4    | reserved                         |
| `+0x1C`    | 4    | flags                            |
| `+0x20`    | 4    | `start_block`                    |
| `+0x24`    | 4    | `block_count`                    |
| `+0x28`    | 4    | `load_addr`                      |
| `+0x2C`    | 4    | reserved                         |

The EBOOT helper `sub_80064B40(index)` returns `entry + 4`, thus most code sees the record from the tag field, not from the raw base.

The older v1.58.2 raw NAND dump stores the same eight-entry table at `PTB+0x230`.

## Default Layout Built by EBOOT

`ptb_build_default_in_ram` emits these eight entries in order:

1. `NBT`
2. `IPL`
3. `BAK`
4. `UDR`
5. `NK`
6. `DSK`
7. `CFG`
8. `END`

The builder uses the runtime NAND geometry:

```
block_size   = sectors_per_block * bytes_per_sector
total_blocks = nand_block_count
```

and fills the table like this:

| Tag | Filename | Flags | Start / Count / Load |
| --- | --- | --- | --- |
| `NBT` | `nboot.bin` | `0x0000000F` | `start=0`, `count=2`, `load=0x00000000` |
| `IPL` | `eboot.nb0` | `0x00000007` | `start=2`, `count=0x00200000 / block_size`, `load=0x80038000` |
| `BAK` | `eboot.bak` | `0x00000007` | `start=IPL.end`, `count=IPL.count`, `load=0x80038000` |
| `UDR` | `nk.nb0` | `0x00001006` | `start=BAK.end`, `count=0x00040000 / block_size`, `load=0x80200000` |
| `NK` | `nk.nb0` | `0x000B3004` | `start=UDR.end`, `count=0x07800000 / block_size`, `load=0x80200000` |
| `DSK` | `disk.img` | `0x000B1000` | `start=NK.end`, `count=CFG.start - NK.end`, `load=0xFFFFFFFF` |
| `CFG` | `config.txt` | `0x00000003` | `start=total_blocks - 7`, `count=3`, `load=0xFFFFFFFF` |
| `END` | `end.txt` | `0x00000013` | `start=total_blocks - 4`, `count=4`, `load=0xFFFFFFFF` |

On the v1.88 NAND geometry, `block_size = 0x40000` and `total_blocks = 2048`, that becomes:

| Tag   | start_block | block_count | size        | load_addr    |
| ----- | ----------- | ----------- | ----------- | ------------ |
| `NBT` | `0`         | `2`         | `512 KiB`   | `0x00000000` |
| `IPL` | `2`         | `8`         | `2 MiB`     | `0x80038000` |
| `BAK` | `10`        | `8`         | `2 MiB`     | `0x80038000` |
| `UDR` | `18`        | `1`         | `256 KiB`   | `0x80200000` |
| `NK`  | `19`        | `480`       | `120 MiB`   | `0x80200000` |
| `DSK` | `499`       | `1542`      | `385.5 MiB` | `0xFFFFFFFF` |
| `CFG` | `2041`      | `3`         | `768 KiB`   | `0xFFFFFFFF` |
| `END` | `2044`      | `4`         | `1 MiB`     | `0xFFFFFFFF` |

`END` is not an empty stop marker in the EBOOT table. The builder gives it a real `start_block`, `block_count`, `flags` and filename, and it keeps the final four blocks reserved behind it.

The v1.58.2 dump uses `block_size = 0x20000` and `total_blocks = 4096`. The same byte-size partition formulas therefore give doubled block counts, for example `IPL.count = 16`, `UDR.count = 2` and `END.start = 4092`.

## Runtime Use

The config and maintenance paths of EBOOT use small numeric partition ids that line up with the PTB entry order:

- `1` targets `IPL`
- `3` targets `UDR` on the boot-menu `[u]` path
- `4` targets `NK`
- `5` targets `DSK`

The base-options menu stores the default boot target in the PTB header field `+0x24`. `fmd_mount` reads the key values as:

- `4`: boot `NK`
- `9`: enter the boot and config menu
- `10`: leave control to the KITL and network-download path

The stock default is `4`.

`oal_bootargs_init` copies the transport field of the PTB header at `+0x28` into BOOTARGS `0xA0020844`. `fmd_read_partition_table` then selects the Ethernet backend from that value:

- `0`: Bulverde RNDIS / `AKUSB`
- non-zero: `ENC28J60`

## NK Image Format

The flash boot path does not treat `NK` as a plain `B000FF` stream. `sub_80065F54` reads the first `68` bytes of the kernel image into RAM, requires `*(base + 0x40) == 'ECEC'`, and then loads the rest of the image. The kernel image here is the content of the NK BINFS sub-partition, not `NK.raw` from byte 0. `NK.raw[0x40]` is the MBR page and holds no ECEC data. [unverified - the EBOOT disassembly does not yet confirm the exact load offset inside the NK PTB partition].

`sub_8005B3E8` runs a second check on the same image:

- it requires `ECEC` at `+0x40` again
- it follows the dword at `+0x44` through the current `rom_offset`
- it walks a 32-byte record array and accepts the image only when one record resolves to `nk.exe`

EBOOT also holds a generic image parser, `nk_partition_load`. It reads from the SimpleTFTP download stream, understands `N000FF` and `B000FF` records, and rejects the old `X000FF` multi-bin manifest outright. Despite its current database name, this function is **not** the flash `NK` partition loader.

## Unresolved

- The meaning of the raw entry word at `+0x00`.
- The per-bit meaning of the entry `flags` fields.
- The exact image format inside `UDR`, beyond three verified facts. The `[u]` path of `fmd_mount` selects tag `UDR`, `sub_80065B70` validates an `IMG` wrapper there, and the reboot helper launches it from the `load_addr` of the PTB entry.
- The full meaning of the opaque header and body region at `+0x2C..+0x66F`. EBOOT preserves it but does not decode it in the audited paths.
- The detailed structure behind the `ECEC` offset at `+0x44`, and the 32-byte record array that `sub_8005B3E8` walks.
