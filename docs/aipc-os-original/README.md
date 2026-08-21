# AIPC OS Original

Original research from us. This directory holds what we invented and what we built, not what we recovered from the device.

Everything under [bootrom](../bootrom/README.md), [nboot](../nboot/README.md), [eboot](../eboot/README.md) and [nk](../nk/README.md) describes the original firmware. Those documents answer "what does this device do". This directory answers "what did we make it do". Some of that is a driver the original firmware also has, built differently. Some of it is a capability the original firmware never had at all.

The split also settles a question that comes up in every driver: does the hardware do this, or did we choose it? A hardware constraint belongs in the reverse-engineering documents. An invention, a design decision or a measurement of our own code belongs here.

## Index

- Johnson-Nyquist Noise TRNG: a true random number generator from the thermal noise that the unconnected L2 SRAM address window returns. The work is under way, and the document is not written yet.
- [Faster SD/MMC Driver](faster-sd-driver.md): request size, clock and DMA choices in the Linux MMC host driver, and the throughput they reach.
- [Ethernet Driver](ethernet-driver.md): the GPIO-bitbanged (software-timed) DM9000 backend, the offloads it enables, and the gap against the mainline driver.
- [Warm Restart](warm-restart.md): the software restart that this device does not have, built from a jump back into the bootrom, and the one condition that makes it reliable.
