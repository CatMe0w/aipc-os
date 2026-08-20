# openNBOOT

A drop-in replacement for the stock bootloader (nboot). It lives in NAND block 0, and the AK7802 bootrom loads it the same way it loads the stock bootloader.

It boots [aipc-boot](../aipc-boot/), or any other ARM payload, from an SD card when a card is present. Without a card it boots stock Windows CE from NAND. An SD payload goes to `0x33000000`. The full DDR address convention is in [../README.md](../README.md).

## Building

```
make
```

The build needs `arm-none-eabi-gcc` for the payload, and host GCC or Clang for the tests.

## Usage

This directory has the code only. The installer and the instructions to use it are in [tools/opennboot](../../tools/opennboot/).

[bootbin](test/bootbin/README.md) is a test payload that runs from an SD card.
