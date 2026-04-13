# USB HID Input

EBOOT's maintenance gate uses a CH374-backed USB HID path. Before the
F1/password UI appears, `maintenance_menu_entry` initializes the SPI
controller used by CH374, runs the CH374 register setup sequence, clears
the HID key-state bytes at `0x801045F0..0x801045F2` and `0x801045F6`,
and then calls `maintenance_menu`.

This document records the SPI-side register usage, the CH374 command and
init sequence, the HID polling path, and the password gate that sits in
front of the maintenance menu.

## SPI Controller Used By CH374

`ch374_init` calls `spi_init_controller(0)`, which selects physical
`0x20024000` and stores its virtual base in `0x80107768`. The numbering
used by the surrounding documentation is still unresolved; this page
records only the physical base that EBOOT actually selects.

The transfer helpers use these offsets relative to the selected SPI
base:

| Offset | Use in EBOOT |
| ------ | ------------ |
| `+0x00` | control |
| `+0x04` | status |
| `+0x0C` | transfer count |
| `+0x10` | cleared before TX |
| `+0x14` | cleared before RX |
| `+0x18` | TX data |
| `+0x1C` | RX data |
| `+0x20` | written once by `spi_config_mode_clock` to `0x00FFFFFF` |

The bit assignments that are directly visible in the transfer path are:

| Register | Bit | Meaning |
| -------- | --- | ------- |
| ctrl `+0x00` | `0` | RX mode select |
| ctrl `+0x00` | `1` | START / busy |
| ctrl `+0x00` | `5` | chip select |
| status `+0x04` | `2` | TX FIFO ready |
| status `+0x04` | `6` | RX data ready |
| status `+0x04` | `8` | transfer complete |

`ch374_init` programs the controller twice through
`spi_config_mode_clock`, first for `1_000_000` Hz and then for
`18_000_000` Hz.

## CH374 Command Path

`ch374_reg_write(reg, value)` sends a 3-byte SPI write under CS:

```text
[reg, 0x80, value]
```

`ch374_reg_read(reg)` uses a different format. It sends `[reg, 0xC0]`
and reads back one response byte:

```text
TX: [reg, 0xC0]
RX: [value]
```

`ch374_read_buffer(cmd, len, dst)` uses the same `0xC0` read marker,
then copies `len` returned bytes into `dst`. `ch374_set_address(addr,
len, buf)` is a two-step command: it first sends `[addr, 0x80]`, then
sends `len` bytes from `buf`.

The initialization order in `maintenance_menu_entry` is
`ch374_init()`, `ch374_register_setup_stage1()`, and
`ch374_register_setup_stage2()`. Since
`ch374_register_setup_stage1()` itself tail-calls stage 2, stage 2 runs
twice in total.

The observed register writes are:

| Step | Register writes |
| ---- | --------------- |
| stage 1 | `6=0`, `8=0`, `14=0`, `9=31`, `7=3`, `5=64` |
| stage 2 | `6=192`, then `2 = reg2 & 0x7F` |

These are CH374-local register numbers. Their chip-level meaning is not
decoded here.

## HID Polling Path

`ch374_poll_hid_keycode(report_buf)` polls up to three tracked USB
device slots. For each slot whose state bytes indicate an attached and
ready device, it selects the slot with `sub_80072A9C(slot)`, writes
`reg9 = 0x11`, issues `ch374_set_address(0xC0, 0x14, tmp)`, and then
calls `ch374_read_hid_report(report_buf, &report_len)`.

`ch374_read_hid_report` succeeds when its return value is `0x14`. On
that path it reads the report length from `reg11`, fetches that many
bytes with `ch374_read_buffer(0xC0, len, report_buf)`, and toggles the
saved data-toggle state for the selected slot.

If `report_len >= 8`, EBOOT passes the buffer to `sub_80070174`, which
interprets it as a boot-keyboard report: byte `0` is the modifier
bitmap, byte `1` is reserved, and bytes `2..7` hold the six concurrent
HID usage slots. `sub_80070174` and `sub_80070000` keep per-port key
state in RAM and forward key transitions into the higher-level UI input
path through `sub_8006FFD0`.

This path carries HID usage codes, not ASCII characters. The usages that
matter to the maintenance gate are:

| Usage | Key |
| ----- | --- |
| `0x3A` | `F1` |
| `0x29` | `Esc` |
| `0x28` | `Enter` |
| `0x1D` | `Z` |
| `0x17` | `T` |
| `0x0E` | `K` |

## Maintenance Password Gate

It first polls for an `F1` keypress (`0x3A`) for `100` iterations with
`delay_ms(10)` between polls. If `F1` is seen, it prompts for the
password and compares the first three typed usage codes against
`{0x1D, 0x17, 0x0E}`. `Enter` (`0x28`) submits the attempt but is not
part of the `memcmp`.

At the user level the password is still:

```text
Z T K Enter
```

See [maintenance-mode.md](maintenance-mode.md) for the menu behavior
after the password check succeeds.

## Unresolved

- The exact CH374 register meanings for registers `2`, `5`, `6`, `7`,
  `8`, `9`, `11`, and `14`.
- The exact physical mapping of the three tracked USB device slots to
  board connectors.
- The higher-level event API behind `sub_8006FFD0` / `sub_8007330C`.
- The full non-HID CH374 paths in EBOOT, including the USB mass-storage
  code paths hinted by strings such as `Not USB Mass Storage Device`.
