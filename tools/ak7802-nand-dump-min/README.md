# ak7802-nand-dump-min

Minimal host-driven NAND dump reference implementation for AK7802 via USB boot mode.

This tool exists because it was the first small implementation that worked reliably while `nand-dump` was still under active development. It targets the same end result as `nand-dump`, but takes a simpler host-driven route: the stub reads one NAND page into SRAM, returns to the bootrom, and the host pulls that page back with the bootrom's upload path.

It is intentionally a reference/minimum implementation. Dumping a 512 MB NAND device takes about 6 hours. On an AIPC, use `opennboot backup-nand` from [opennboot](../opennboot/).

## How it works

1. Host uploads the stub (once) to 0x48000240.
2. For each operation, host writes a parameter block to 0x48000040 via the bootrom's DOWNLOAD command, then issues EXECUTE.
3. The stub runs: calls ROM helpers to read raw NAND data plus OOB/spare into 0x48000400.
4. The stub returns to the bootrom (it's a function call, not a jump).
5. Host reads back data from 0x48000400 via the bootrom's UPLOAD command.
6. Repeat from step 2 for the next page.

The stub never touches USB. The host must send nothing on the bulk endpoints while the stub runs, or the packet can replace bytes of the page. `ak7802-usbboot` waits for the stub with requests on EP0 for this reason, see [usb-boot.md](../../docs/bootrom/usb-boot.md). **Dumps made before that change (2026-11-13) can have damage in the first 64 bytes of a page.**

## Building the stub

Requires `arm-none-eabi-gcc`.

```
cd stub
make
```

## Running

```
uv run ak7802-nand-dump-min -o dump.img
```

The device must be in USB boot mode (`DL_JUMP` or `USB_BOOT` connected at power-on).

## Memory layout during EXECUTE

| Address range | Size | Used by |
| --- | --- | --- |
| `0x48000000 - 0x4800003F` | 64 B | USB TX staging (HW-managed) |
| `0x48000040 - 0x4800007F` | 64 B | Parameter block |
| `0x48000240 - 0x480003FF` | <=448 B | Stub code |
| `0x48000400 - ...` | page-dependent | Raw page output area (data + OOB/spare) |
| `0x48001100 - 0x4800157B` | ~1.1 KB | Stack |
