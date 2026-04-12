# GPIO Driver

EBOOT's GPIO driver sits on top of the raw SYSCTRL registers documented in
the bootrom `memory-map.md`. It adds a small driver layer with a fixed set
of helper functions and introduces two independent GPIO numbering systems
that are easy to confuse because they share the same value space.

## Two Independent Numbering Systems

EBOOT code routinely passes "a GPIO number" to two families of helpers that
mean **different things**:

1. **Physical pin number**, range `0..117`. This is the same linear space
   used in the bootrom GPIO crosswalk: `bank * 32 + bit_in_bank`. Bank 0
   corresponds to `GPIO1`, bank 1 to `GPIO2`, bank 2 to `GPIO3`, bank 3 to
   `GPIO4`. The maximum value `0x75 = 117` is enforced as a bounds check
   inside the driver.

2. **Alt function ID**, range `0..56`. This is an unrelated 57-entry index
   into a function-pointer table in `.data`. Each entry is a small stub
   that knows which sharepin-mux bits correspond to one specific alt
   function and writes those bits directly. Alt function ID `N` has no
   relation to physical pin `N`.

The two spaces overlap numerically (both can hold, for example, the value
20), which causes endless confusion when reading decompiler output. A code
snippet like `gpio_enable_alt(20)` is an **alt function ID**, not "GPIO
pin 20".

When the two must coexist in the same analysis, the convention used in
these docs is:

- "physical pin", "pin number", or a bank/bit pair (`GPIO1[9]`) means the
  physical space.
- "alt function ID" or "alt ID" means the 0..56 space.

## GPIO Register Layout

The relevant SYSCTRL offsets (all relative to SYSCTRL base `0x08000000`):

| Offset | Description                                              |
| ------ | -------------------------------------------------------- |
| +0x74  | Sharepin mux register 0                                  |
| +0x78  | Sharepin mux register 1 (mixed bit polarity; see below)  |
| +0x7C  | GPIO1 direction (bank 0 dir, 1 = input, 0 = output)      |
| +0x80  | GPIO1 output data                                        |
| +0x84  | GPIO2 direction                                          |
| +0x88  | GPIO2 output data                                        |
| +0x8C  | GPIO3 direction                                          |
| +0x90  | GPIO3 output data                                        |
| +0x94  | GPIO4 direction                                          |
| +0x98  | GPIO4 output data                                        |
| +0x9C  | GPIO bank 0 auxiliary config `[partial]`                 |
| +0xA0  | GPIO bank 1 auxiliary config `[partial]`                 |
| +0xA4  | GPIO bank 2 auxiliary config `[partial]`                 |
| +0xA8  | GPIO bank 3 auxiliary config `[partial]`                 |
| +0xBC  | GPIO1 input data (read-only)                             |
| +0xC0  | GPIO2 input data (read-only)                             |
| +0xC4  | GPIO3 input data (read-only)                             |
| +0xC8  | GPIO4 input data (read-only)                             |
| +0xD4  | I/O control register; bank-0 input filter/wake enable    |
| +0xE0  | GPIO1 interrupt status `[hypothesis]`                    |
| +0xE4  | GPIO2 interrupt status `[hypothesis]`                    |
| +0xE8  | GPIO3 interrupt status `[hypothesis]`                    |
| +0xEC  | GPIO4 interrupt status `[hypothesis]`                    |
| +0xF0  | GPIO1 interrupt mask (1 = masked) `[hypothesis]`         |
| +0xF4  | GPIO2 interrupt mask `[hypothesis]`                      |
| +0xF8  | GPIO3 interrupt mask `[hypothesis]`                      |
| +0xFC  | GPIO4 interrupt mask `[hypothesis]`                      |

Physical pin `N` maps to bank `(N >> 5) & 3` and bit `N & 0x1F`. The
direction register for bank `B` is at `SYSCTRL + 0x7C + 8*B`; the output
register is at `SYSCTRL + 0x80 + 8*B`; the input register is at
`SYSCTRL + 0xBC + 4*B`; the aux register is at `SYSCTRL + 0x9C + 4*B`.

### GPIO4 Input Data Alignment

GPIO4 (bank 3) has a 3-bit positional offset between its output register
(`+0x98`) and its input register (`+0xC8`): output bit `N` corresponds to
input bit `N + 3`, not `N`. EBOOT handles this inside the read helper by
shifting the input data word left by 3 when the caller requests a pin in
bank 3. The bootrom GPIO crosswalk reflects the same phenomenon in its
`GPIO4[in 5], GPIO4[8]` style entries.

This offset does not apply to banks 0-2, only bank 3.

## Driver Function Layer

All helpers read the SYSCTRL virtual base from a single global pointer set
at init time; no absolute SYSCTRL addresses appear inside the driver
functions. The parameter passed as "pin" in the table below is the
**physical pin number** (0..117) unless noted otherwise.

### `gpio_bank_config_write(pin, direction)`

Sets a pin's direction. `direction == 0` clears the bit in the direction
register (output); any other value sets it (input).

Side effect unique to bank 0: if a pin in bank 0 is configured as input,
the driver looks up a byte in a 32-entry lookup table and, if the value is
not `0xFF`, ORs a single bit into `SYSCTRL + 0xD4` at the position given by
the table entry. The table has one entry per bit in GPIO1. Its purpose is
plausibly to enable a per-pin input filter or wake-from-sleep source for
the small subset of GPIO1 pins that support it, but the exact semantics of
SYSCTRL+0xD4 are not confirmed.

This side effect is only triggered for bank 0 inputs; banks 1-3 do not
touch `+0xD4`.

### `gpio_bank_data_write(pin, value)`

Writes a pin's output bit. `value == 0` clears, any other value sets.
Targets `SYSCTRL + 0x80 + 8*bank`.

### GPIO read helper

Reads a pin's input bit and returns it as 0 or 1. Reads from
`SYSCTRL + 0xBC + 4*bank`. Applies the 3-bit left shift described above
for bank 3.

### `gpio_enable_alt(alt_id)`

Enables the alt function identified by `alt_id` (0..56). Looks up the
function pointer at index `alt_id` in the dispatch table and invokes it.
The target stub writes a specific bit or group of bits in either
`SYSCTRL + 0x74` or `SYSCTRL + 0x78`.

The dispatch table lives at runtime address `0x800F0140` in `.data` and is
228 bytes long (57 entries * 4 bytes). It is copied there from the
read-only image during EBOOT's `.data` setup. A readiness check gates
`gpio_enable_alt` calls that might fire before the copy completes.

### `gpio_aux_config_write(pin, value)` and its companion

Two helpers write to the aux registers `+0x9C..+0xA8`. They cover
**complementary sets of pins** and are dispatched based on a per-pin mode
value obtained elsewhere (the maintenance path uses the constant `9` to
select one of the two). Both helpers set a single bit in one of the aux
registers, with the bit index derived from `pin mod 32`.

The aux registers themselves are `[partial]`: the single-bit-per-pin
encoding is observed, but whether the bit controls pull-up, pull-down,
input filter, debounce, or drive strength is not determined.

### Indirect helper for variable pins

A small wrapper accepts a pointer to a pin number, NULL-checks it, and
forwards to `gpio_enable_alt`. It is used in paths where the pin number
comes from a configuration table that may contain a sentinel.

## Sharepin Mux Registers and Polarity

`SYSCTRL + 0x74` and `SYSCTRL + 0x78` are the two sharepin mux registers
that select whether each pad is routed to its primary peripheral function
or to a GPIO. EBOOT's alt-function stubs exclusively touch these two
registers, and the per-stub polarity is mixed:

- Some stubs OR a bit into `+0x78` when the alt function is being enabled.
- Other stubs AND-NOT a bit into `+0x78` when the alt function is being
  enabled.

The driver exposes two internal helpers, one for each polarity. Each alt
function stub picks the correct one based on how the underlying hardware
encodes its enable bit. The reason is that different peripheral groups
have different "1 = enable / 0 = enable" conventions hardwired into the
chip, and the stubs encapsulate that knowledge.

## Default State After `sysctrl_clock_init`

During early init, EBOOT writes a known-state pattern to the GPIO
registers:

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

The "clear status, mask all" pattern in `+0xE0..+0xFC` is the basis for
the GPIO interrupt controller hypothesis. EBOOT itself never unmasks any
of these bits at runtime - it is a polling-mode bootloader - so the
interrupt path remains unexercised through the EBOOT lifetime.

Following this register setup, the same init function enables eight
mandatory alt functions by calling `gpio_enable_alt` with IDs `44, 8, 53,
13, 12, 16, 51, 52`. These IDs are the prerequisites for the later driver
inits (NAND, SPI0, SPI2, UART, LCD). They are referenced by ID only; the
mapping from ID to physical pin is not tabulated in this documentation
(see `Unresolved` below).

## Bank-0 Input Filter Table

The 32-byte lookup table referenced by `gpio_bank_config_write` lives at
`0x8010011C` in `.data`. It has one byte per bit in GPIO1 (index 0..31).
During `.data` initialization, most entries are `0xFF` ("no action"), and
a small set of entries hold valid bit indices into `SYSCTRL + 0xD4`.

The exact table contents are not independently listed here because they
depend on runtime `.data` initialization that is not fully captured in the
clean binary dump. The bootrom's `diag-mode.md` documents that the
bootrom sets `SYSCTRL + 0xD4` bits `[17:2]` and `[27:26]` - a total of 18
bits - which is consistent with "18 out of 32 GPIO1 pins have an entry in
the filter table".

## Cross-Reference

The mapping between physical pin number, the SoC pin name on the AK7802
QFP216 schematic, and the actual board net is documented separately in
[docs/bootrom/gpio-naming-crosswalk.md](../bootrom/gpio-naming-crosswalk.md).
That document is authoritative for "which pin is which signal". EBOOT's
driver layer is consistent with the crosswalk with no corrections needed.

Key signals referenced from other EBOOT docs:

- `GPIO1[9]` = `WLED_PWM`, the LCD backlight PWM output
  (see [lcd-driver.md](lcd-driver.md))
- `GPIO4[8]` = `DGPIO2` = `USB_BOOT` strap pin, held high to prevent
  re-entry into USB boot mode after warm reset

## Unresolved

- Aux registers `SYSCTRL+0x9C..+0xA8`: single-bit-per-pin encoding
  confirmed, semantic meaning unknown.
- GPIO interrupt registers `SYSCTRL+0xE0..+0xFC`: hypothesized from
  init-time values only; no driver path exercises them in EBOOT.
- `SYSCTRL+0xD4`: bit-level semantics of the bank-0 input filter / wake
  enable register not confirmed.
- Alt function ID to physical pin mapping: the 57-entry table in
  `.data` at `0x800F0140` is understood structurally (each entry is a
  stub writing to `+0x74`/`+0x78`), but no complete ID-to-pin table has
  been built. Building it requires walking all 57 stubs, recording the
  bit mask each one writes, and then cross-referencing the bit position
  against the pin assignments encoded in the sharepin mux registers.
- Bank-0 input filter table at `0x8010011C`: observed as a 32-byte
  `0xFF`-sentinel table, exact non-sentinel values not tabulated in this
  documentation because they depend on ROM-initialized `.data` that was
  not independently verified against the binary.
