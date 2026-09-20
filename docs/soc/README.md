# SoC peripherals

Hardware reference for AK7802 peripheral blocks. It describes what the hardware does, not how any firmware drives it.

AK7802 has no published datasheet. The [AK98 kernel source](https://github.com/onyx-intl/ak98_kernel) describes some of these blocks, but AK98 is a different chip and its description does not always match the AK7802. The [Intrisit8000](https://github.com/DanielGit/Intrisit8000) BSP carries register headers for the AK7801 and AK7802 themselves, under `Include/platform/`, but those headers still contain errors. Prefer this directory when it disagrees with either of them.

## Index

- [timer](timer.md): the five system controller timers. Register model, the automatic reload, the LOAD strobe and the false interrupt it raises, interrupt mapping, clock rate, read cost, and which timers other firmware already uses.
- [memory controller](memory-controller.md): the DDR controller at `0x2002D000`. Register model, the command words, the self refresh sequence for a clock change, the DQS delay, the mode register, and the absence of a /3 clock divider.
- [audio](audio.md): the stereo DAC on the die. Register model, the clock chain and the rates it reaches, the sample format, the L2 data path, the start order the analog stages need, and how this board wires the amplifiers and the jacks.
