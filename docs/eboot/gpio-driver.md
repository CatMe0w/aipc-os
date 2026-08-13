# GPIO Driver

The GPIO driver of EBOOT sits on top of the raw SYSCTRL registers in the bootrom `memory-map.md`. It adds a small driver layer with a fixed set of helper functions, and it introduces two independent GPIO numbering systems. The two are easy to confuse, because they share the same value space.

## Two Independent Numbering Systems

EBOOT code routinely passes "a GPIO number" to two families of helpers that mean **different things**:

1. **Physical pin number**, range `0..117`. This is the linear space of the bootrom GPIO crosswalk: `bank * 32 + bit_in_bank`. Bank 0 is `GPIO1`, bank 1 is `GPIO2`, bank 2 is `GPIO3`, bank 3 is `GPIO4`. The driver enforces the maximum value `0x75 = 117` as a bounds check.

2. **Alt function ID**, range `0..56`. This is an unrelated 57-entry index into a function-pointer table in `.data`. Each entry is a small stub. It knows which sharepin-mux bits belong to one specific alt function, and it writes those bits directly. Alt function ID `N` has no relation to physical pin `N`.

The two spaces overlap numerically, because both can hold the value 20, for example. This causes endless confusion in decompiler output. A snippet such as `gpio_enable_alt(20)` carries an **alt function ID**, not "GPIO pin 20".

Where the two must coexist in one analysis, these documents use this convention:

- "physical pin", "pin number", or a bank and bit pair (`GPIO1[9]`) means the physical space.
- "alt function ID" or "alt ID" means the 0..56 space.

## GPIO Register Layout

The relevant SYSCTRL offsets, all relative to the SYSCTRL base `0x08000000`:

| Offset | Description                                             |
| ------ | ------------------------------------------------------- |
| +0x74  | Sharepin mux register 0                                 |
| +0x78  | Sharepin mux register 1 (mixed bit polarity; see below) |
| +0x7C  | GPIO1 direction (bank 0 dir, 1 = input, 0 = output)     |
| +0x80  | GPIO1 output data                                       |
| +0x84  | GPIO2 direction                                         |
| +0x88  | GPIO2 output data                                       |
| +0x8C  | GPIO3 direction                                         |
| +0x90  | GPIO3 output data                                       |
| +0x94  | GPIO4 direction                                         |
| +0x98  | GPIO4 output data                                       |
| +0x9C  | GPIO bank 0 pull-up/pull-down `[partial]`               |
| +0xA0  | GPIO bank 1 pull-up/pull-down `[partial]`               |
| +0xA4  | GPIO bank 2 pull-up/pull-down `[partial]`               |
| +0xA8  | GPIO bank 3 pull-up/pull-down `[partial]`               |
| +0xBC  | GPIO1 input data (read-only)                            |
| +0xC0  | GPIO2 input data (read-only)                            |
| +0xC4  | GPIO3 input data (read-only)                            |
| +0xC8  | GPIO4 input data (read-only)                            |
| +0xD4  | I/O control register; bank-0 input filter/wake enable   |
| +0xE0  | GPIO1 interrupt enable                                  |
| +0xE4  | GPIO2 interrupt enable                                  |
| +0xE8  | GPIO3 interrupt enable                                  |
| +0xEC  | GPIO4 interrupt enable                                  |
| +0xF0  | GPIO1 interrupt polarity (1 = active low)               |
| +0xF4  | GPIO2 interrupt polarity                                |
| +0xF8  | GPIO3 interrupt polarity                                |
| +0xFC  | GPIO4 interrupt polarity                                |

Physical pin `N` maps to bank `(N >> 5) & 3` and bit `N & 0x1F`. For bank `B`, the direction register is at `SYSCTRL + 0x7C + 8*B`, the output register at `SYSCTRL + 0x80 + 8*B`, the input register at `SYSCTRL + 0xBC + 4*B`, and the aux register at `SYSCTRL + 0x9C + 4*B`.

### GPIO4 Input Data Alignment

The GPIO read helper (`sub_800629BC`) treats bank 3 specially, and only for physical pins `99..117` (`0x63..0x75`). Before it tests the requested bit, it shifts the `SYSCTRL + 0xC8` word left by 3. This aligns `GPIO4[in N]` with logical pin `GPIO4[N+3]`, which matches the `GPIO4[in 5], GPIO4[8]` style entries in the bootrom crosswalk.

Pins `96..98` read without that shift. Banks 0 to 2 also read with no adjustment.

## Driver Function Layer

Every helper reads the SYSCTRL virtual base from one global pointer, set at init time. No absolute SYSCTRL address appears inside a driver function. The parameter named "pin" in this section is the **physical pin number**, 0..117, unless the text says otherwise.

### `gpio_bank_config_write(pin, direction)`

Sets the direction of a pin. A `direction == 0` clears the bit in the direction register, which makes the pin an output. Any other value sets the bit, which makes it an input.

Bank 0 has a side effect of its own. When a pin in bank 0 becomes an input, the driver looks up a byte in a 32-entry lookup table. If the value is not `0xFF`, the driver ORs a single bit into `SYSCTRL + 0xD4` at the position from the table entry. The table has one entry per bit in GPIO1. Its purpose is plausibly a per-pin input filter, or a wake-from-sleep source, for the small subset of GPIO1 pins that support one. The exact semantics of SYSCTRL+0xD4 are unconfirmed.

Only a bank 0 input triggers this side effect. Banks 1 to 3 do not touch `+0xD4`.

### `gpio_bank_data_write(pin, value)`

Writes the output bit of a pin. A `value == 0` clears the bit, and any other value sets it. The target is `SYSCTRL + 0x80 + 8*bank`.

### GPIO read helper

Reads the input bit of a pin and returns it as 0 or 1. It reads from `SYSCTRL + 0xBC + 4*bank`. For physical pins `99..117` it applies the 3-bit left shift above before it tests the bit.

### `gpio_enable_alt(alt_id)`

Enables the alt function that `alt_id` identifies, 0..56. It looks up the function pointer at index `alt_id` in the dispatch table and calls it. The target stub always updates the sharepin state through SYSCTRL. It usually sets or clears bits in `SYSCTRL + 0x78`, and for some IDs it also edits fields in `SYSCTRL + 0x74` through `reg_rmw`.

The dispatch table lives at runtime address `0x800F0140` in `.data`, and it is 228 bytes long, 57 entries of 4 bytes. `gpio_enable_alt` also calls `gpio_table_ready`, which walks a runtime-populated interval table at `0x800F0270` and rejects an ID outside the allowed ranges. Its sibling `sub_800638CC` does the same range check for related wrappers. This is an ID validity check, not a readiness check.

### `gpio_aux_config_write(pin, value)` and its companion

Two helpers write to the aux registers `+0x9C..+0xA8`. A per-pin mode value from elsewhere selects between them. The maintenance path uses the constant `9` to select `sub_80062BA4`, and otherwise it uses `gpio_aux_config_write`. Each helper has its own hard-coded sparse pin allowlist. The two sets are not simply complementary, and they overlap on GPIO1 pins `16..27`.

The two helpers have **opposite polarity**:

- `sub_80062BA4`: `value == 0` sets the selected aux bit, and any non-zero value clears it.
- `gpio_aux_config_write`: the polarity depends on the pin range.
  - Pins `16..27` (GPIO1): `value != 0` sets the bit, and `value == 0` clears it. This is the **inverse** of `sub_80062BA4`.
  - Pins `28..31`, `14..15`, `12`, `1..3`: `value == 0` sets the bit, and `value != 0` clears it, the same as `sub_80062BA4`.

The established names for `+0x9C..+0xA8` are the per-bank pull-up and pull-down registers. EBOOT alone does not confirm that. The single-bit-per-pin encoding is visible, but two things are not determined: which of pull-up, pull-down or a shared enable the bit selects, and how that fits with two helpers that write it with opposite polarity.

### Indirect helper for variable pins

A small wrapper accepts a pointer to a pin number, checks it for NULL, and forwards to `gpio_enable_alt`. Paths where the pin number comes from a configuration table with a sentinel use it.

## Sharepin Mux Registers and Polarity

`SYSCTRL + 0x74` and `SYSCTRL + 0x78` are the two sharepin mux registers. They select whether each pad goes to its primary peripheral function or to a GPIO. The alt-function stubs of EBOOT touch only these two registers, and the per-stub polarity is mixed:

- Some stubs OR a bit into `+0x78` to enable the alt function.
- Other stubs AND-NOT a bit into `+0x78` to enable the alt function.
- Some stubs also update fields in `+0x74` with `reg_rmw(base+0x74, set_mask, clear_mask)` before they touch `+0x78`.

The driver exposes two internal helpers, one per polarity. Each alt function stub picks the correct one, from the way the hardware encodes its enable bit. Different peripheral groups have a different "1 = enable" or "0 = enable" convention hardwired into the chip, and the stubs hold that knowledge.

## Default State After `sysctrl_clock_init`

During the early init, EBOOT writes a known-state pattern to the GPIO registers:

```
SYSCTRL+0x7C = 0xFFFFFFFF   # GPIO1 all inputs
SYSCTRL+0x80 = 0            # GPIO1 all outputs = 0
SYSCTRL+0x84 = 0xFFFFFFFF   # GPIO2 all inputs
SYSCTRL+0x88 = 0
SYSCTRL+0x8C = 0xFFFFFFFF   # GPIO3 all inputs
SYSCTRL+0x90 = 0
SYSCTRL+0x94 = 0xFFFFFFFF   # GPIO4 all inputs
SYSCTRL+0x98 = 0

SYSCTRL+0xE0 = 0            # GPIO1 int status cleared
SYSCTRL+0xE4 = 0            # GPIO2 int status cleared
SYSCTRL+0xE8 = 0            # GPIO3 int status cleared
SYSCTRL+0xEC = 0            # GPIO4 int status cleared

SYSCTRL+0xF0 = 0xFFFFFFFF   # GPIO1 int fully masked
SYSCTRL+0xF4 = 0xFFFFFFFF   # GPIO2 int fully masked
SYSCTRL+0xF8 = 0xFFFFFFFF   # GPIO3 int fully masked
SYSCTRL+0xFC = 0xFFFFFFFF   # GPIO4 int fully masked
```

The `+0xE0..+0xFC` block is the GPIO interrupt controller. EBOOT zeroes it at init and never unmasks anything at runtime, because it is a polling-mode bootloader. The interrupt path therefore stays unexercised for the whole life of EBOOT. The split between the two halves is not visible from EBOOT alone. The established naming has `+0xE0..+0xEC` as the per-bank interrupt enable and `+0xF0..+0xFC` as the per-bank interrupt polarity, where a set polarity bit means active low.

There is no separate pending register. A consumer finds the asserting pin by a comparison of the live input register against the polarity register, for the pins that it enabled.

In `hw_phase1_init`, after `sysctrl_clock_init`, `hw_phase1_step2` and `hw_phase1_step3`, EBOOT enables eight mandatory alt functions. It calls `gpio_enable_alt` with IDs `44, 8, 53, 13, 12, 16, 51, 52`. These IDs are the prerequisites for the later driver inits: NAND, SPI0, SPI2, UART and LCD. The code references them by ID only. This documentation does not tabulate the mapping from ID to physical pin. See `Unresolved` below.

## Bank-0 Input Filter Table

The 32-byte lookup table that `gpio_bank_config_write` uses lives at runtime address `0x800F011C`. It has one byte per bit in GPIO1, index 0..31. The helper treats `0xFF` as "no action". Otherwise it uses the byte as a bit index into `SYSCTRL + 0xD4`.

In the current clean image this RAM-resident table reads as all `0xFF`. The static image therefore does not preserve the final runtime per-pin mapping, and this document claims no specific non-`0xFF` entry.

## Cross-Reference

[docs/bootrom/gpio-naming-crosswalk.md](../bootrom/gpio-naming-crosswalk.md) documents the mapping between the physical pin number, the SoC pin name on the AK7802 QFP216 schematic, and the board net. That document is authoritative for "which pin is which signal". The driver layer of EBOOT agrees with the crosswalk, and it needs no correction.

Key signals that other EBOOT documents reference:

- `GPIO1[9]` = `WLED_PWM`, the LCD backlight PWM output (see [lcd-driver.md](lcd-driver.md))
- `GPIO4[8]` = `DGPIO2` = the `USB_BOOT` strap pin, held high to prevent a re-entry into USB boot mode after a warm reset

## Unresolved

- Pull-up and pull-down registers `SYSCTRL+0x9C..+0xA8`. The single-bit-per-pin encoding is confirmed and the name is the established one, but EBOOT behavior does not confirm the per-bit meaning.
- GPIO interrupt registers `SYSCTRL+0xE0..+0xFC`. The names are the established ones. No EBOOT driver path exercises them.
- `SYSCTRL+0xD4`. The bit-level semantics of the bank-0 input filter and wake enable register are unconfirmed.
- The alt function ID to physical pin mapping. The 57-entry table in `.data` at `0x800F0140` is understood structurally, because each entry is a stub that touches `+0x78` and sometimes `+0x74`, but no complete ID-to-pin table exists yet. To build one, walk all 57 stubs, record the masks that they write, and cross-reference the bit positions against the sharepin assignments.
- The alt-function validator table at `0x800F0270`. Structurally this is a runtime interval table that `gpio_table_ready` and `sub_800638CC` walk, but this document does not tabulate the final interval contents.
- The bank-0 input filter table at `0x800F011C`. This documentation does not tabulate the exact non-`0xFF` values, because the clean binary dump does not preserve the final runtime contents.
