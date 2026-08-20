# AIPC Ethernet pre-validation

This suite checks that the DM9000A parallel bus can be driven from software before the Linux driver depends on it.

## Running

```
make
uv run ak7802-usbboot write --addr 0x48000240 PROBE.bin
uv run ak7802-usbboot exec --addr 0x48000240
uv run ak7802-usbboot read --addr 0x48001100 --len 0x100 out.bin
```

Power-cycle between runs.

## Result layout

Results are 32-bit words from `0x48001100`. Word 0 is a magic identifying the stub and word 1 becomes 1 only if the stub ran to completion; a zero there means a crash rather than a failed measurement.

`dm9000_id_probe`, magic `"DM9A"`:

| Word | Contents |
| --- | --- |
| 4..10 | SYSCTRL snapshot before any writes |
| 12..18 | SYSCTRL snapshot after bus setup |
| 20..26 | `VIDL VIDH PIDL PIDH NCR NSR CHIPR` |
| 32..38 | the same seven registers after an `NCR` reset |
| 44..50 | SYSCTRL snapshot at exit |

`dm9000_idxread_probe`, magic `"DM9I"`:

| Word | Contents |
| --- | --- |
| 2, 3 | idle bus sample, immediately and after a delay |
| 4..8 | `VIDL VIDH PIDL PIDH CHIPR` sanity check |
| 12 + 4n | case `n`: index written, data read, readback, readback after delay |
| 36 | readback after two index writes with data reads between |
| 44..50 | SYSCTRL snapshot at exit |

Each SYSCTRL snapshot is `0x74`, `0x78`, `0x84`, `0x88`, `0xC0`, `0x94`, `0x98`.

## Probe sources

`dm9000_bus.h` holds the pin definitions and the bus primitives, so both stubs generate identical cycles. Changing the timing there changes both.

`dm9000_id_probe.c` brings the bus up and reads the identification and status registers, once before and once after an `NCR` software reset. The SYSCTRL snapshots let pin mux, direction, and level programming be checked independently of the bus result.

`dm9000_idxread_probe.c` answers whether the index port reads back, which a driver needs in order to save the index register across an interrupt. A floating data bus returns whatever was last driven onto it and will imitate a working readback, so each case interposes a data read of a register whose contents differ from the index being tested. The idle samples and the delayed second readback catch a decaying capacitive value.

## Confirmed boundaries

The controller is powered and answers in USB boot mode, before any vendor firmware has run. With `SYSCTRL + 0x78` bit 0 cleared, the twelve `GPIO2` pins and `GPIO4[6]` are controllable and produce working bus cycles. That register reads `0x00000203` on a cold device, so bit 0 is the only bit the sequence changes.

Identification returns `0x0A46` / `0x9000` with `CHIPR` `0x19`, unchanged across an `NCR` software reset.

The index port reads back. Across index values `0x28` through `0x2C`, whose contents differ from the index in every case, the readback returned the index every time and the register contents never, with and without an added delay. An idle bus with all eight data pins as inputs reads `0x00`.

The tested scope is register access only. Neither stub exercises the packet SRAM, the PHY, interrupt delivery, or sustained throughput.
