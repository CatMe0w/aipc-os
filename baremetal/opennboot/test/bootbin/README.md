# SD boot test payload

A stand-in for a real `BOOT.BIN`. It records the state that openNBOOT handed it, checksums its own image, and halts. USB boot mode then reads the result block back.

## Building

```
make
```

`pack.py` pads the stub to 300000 bytes with deterministic filler, then stamps the length into the image.

## Running

Put `BOOT.BIN` in the root directory of the first FAT16 or FAT32 partition of an MBR-partitioned card. Insert the card, then boot openNBOOT. After that, with the device back in USB boot mode:

```
uv run aipc-ddr-init --firmware 1.88
uv run ak7802-usbboot read --addr 0x301E0000 --len 0x20 result.bin
```

## Result block

| Offset | Contents |
| --- | --- |
| +0x00 | magic `0x42424E4F` |
| +0x04 | CPSR at entry |
| +0x08 | SP at entry |
| +0x0C | address entered at |
| +0x10 | stamped image length |
| +0x14 | word sum over the image |
| +0x18 | done marker `0x600D600D` |

Every rebuild makes `pack.py` print the expected value of each field. The length and the word sum change with the build. The other four are constants.
