# ak7802-nand-dump

Dump NAND flash contents from an AK7802 device via USB boot mode.

A bare-metal stub is uploaded to the device's L2 buffer SRAM and executed.
The stub initializes the NAND controller, detects the flash geometry via
Read ID, and streams all pages back to the host over the existing USB
connection. DDR is not required.

## Building the stub

Requires `arm-none-eabi-gcc`.

```
cd stub
make
```

## Running

```
ak7802-nand-dump -o dump.bin
```

The device must be in USB boot mode (DGPIO[2] high at power-on).

Options:

- `--stub PATH` -- path to a prebuilt `stub.bin` (default: `stub/stub.bin`)
- `--timeout MS` -- per-packet USB read timeout (default: 5000)

## How it works

1. The host uploads `stub.bin` to `0x48000200` (L2BUF) and executes it.
2. The stub takes over the bootrom's USB state (no re-enumeration).
3. It sends a 64-byte header containing the NAND ID and detected geometry.
4. It reads every page via the NF sequencer/DMA and streams data back
   through USB EP2 bulk IN, 64 bytes at a time.
5. A zero-length packet signals completion.

## Memory layout

The stub runs entirely in L2 buffer SRAM with no DDR access.
Conservative layout assuming 6 KB of L2 buffer:

| Address range             | Size  | Usage                  |
| ------------------------- | ----- | ---------------------- |
| `0x48000000 - 0x480001FF` | 512 B | NAND DMA / USB staging |
| `0x48000200 - 0x48000DFF` | 3 KB  | Stub code              |
| `0x48000E00 - 0x48000FFF` | 512 B | Temp buffer            |
| `0x48001000 - 0x480017F0` | ~2 KB | Stack                  |
