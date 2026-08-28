# SoC peripherals

Hardware reference for AK7802 peripheral blocks, recovered by measurement on the device instead of from any firmware.

AK7802 has no published datasheet. The AK98 kernel source describes some of these blocks, but AK98 is a different chip and its description does not always match. Prefer this directory when the two disagree.

## Index

- [timer](timer.md): the five system controller timers. Register model, the automatic reload, the LOAD strobe, interrupt mapping, clock rate, and read cost.
