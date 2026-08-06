# openNBOOT

A drop-in replacement for the stock bootloader (nboot). It lives in NAND block 0 and is loaded by the AK7802 bootrom, the same way the stock bootloader is.

It boots aipc-boot or any ARM payload from SD card when one is present, and otherwise boots stock Windows CE from NAND.

## Building

```
make
```

Requires `arm-none-eabi-gcc` for the payload, host GCC or Clang for the tests.

## Trying openNBOOT

You can try openNBOOT first before installing it to NAND.

To boot openNBOOT from RAM immediately, put the device into USB boot mode and run:

```
uv run tools/run.py
```

To retrieve the log after a power cycle, put the device back into USB boot mode and run:

```
uv run tools/dump_log.py
```

Also see [bootbin](test/bootbin/README.md) for a test payload that can be run from SD card.

## Installing openNBOOT

To install openNBOOT to NAND, put the device into USB boot mode and run:

```
uv run tools/install.py
```
 
The installer will back up the stock bootloader to `backup_nboot-<datetime>.bin` in the current directory. The device will boot openNBOOT on the next power cycle.

## Restoring stock bootloader

```
uv run tools/restore.py --image backup_nboot-<datetime>.bin
```

The device will boot the stock bootloader on the next power cycle.
