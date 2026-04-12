# Boot Flow

This document describes the top-level execution path through EBOOT, from
the handoff from nboot to the jump into the WinCE NK kernel. It ties
together the driver-level documents in the rest of this directory.

## Entry

EBOOT is loaded into DDR by nboot. The precise layout at entry is
documented in [docs/nboot/boot-flow.md](../nboot/boot-flow.md); a short
summary:

- nboot reads the first `0x64000` bytes of the `IPL` NAND partition
  into DDR starting at physical `0x30037FD4`.
- The first `0x2C` bytes of that region are the `IMG` wrapper header
  that describes EBOOT as a WinCE image.
- The first EBOOT instruction lives at physical `0x30038000` (virtual
  `0x80038000` after the MMU and OEMAddressTable come up later).
- nboot branches to `0x30038000` in SVC mode with SP already set up
  for EBOOT's use.

At physical `0x30038000`, the ARM assembly entry branches to the ARM
relocation/setup routine which performs the minimum required work to
reach the C entry point.

## C Entry Point: `eboot_main`

After the ARM entry prepares BSS, stack, and a preliminary MMU
configuration, it calls `eboot_main`, which is EBOOT's equivalent of
a C `main()` function. The high-level sequence inside `eboot_main`:

1. **BSS and `.data` initialization.** Copies initialized `.data`
   from the image's read-only region into `.data`'s runtime
   addresses. Zeroes BSS. This is the step that populates the
   alt-function dispatch table at `0x800F0140`, the input-filter
   lookup table at `0x8010011C`, and all the other compile-time
   tables referenced from across the driver layer.

2. **`oem_early_init`.** Installs the WinCE OEMAddressTable so that
   subsequent `OALPAtoVA` calls can translate physical to virtual
   addresses. Sets up the SYSCTRL virtual base pointer that the GPIO
   driver and everything else reads from. Configures the CPU clock
   if needed.

3. **`oem_platform_init`.** Runs the full hardware init sequence
   described in the next section. Returns when the hardware is ready
   and the maintenance menu has either been skipped or completed.

4. **Load NK.** If the maintenance menu did not redirect the boot
   path (e.g. by selecting a TFTP download), EBOOT locates the `NK`
   partition in the PTB, calls the NAND load path to copy it into
   DDR at virtual `0x80200000`, and then transfers control to the
   loaded image.

5. **Never returns.** The jump into NK is final. EBOOT's code and
   data regions remain in DDR but are no longer referenced by
   running code; NK allocates its own working memory starting from
   its load address.

## `oem_platform_init`

`oem_platform_init` is EBOOT's full platform bring-up function. It
runs all hardware drivers' init routines in a fixed order and then
enters the interactive menu loop. The top-level structure:

```
oem_platform_init():
    hw_phase1_init()
    hw_phase1_step2()
    hw_phase1_step3()
    <additional driver init: NAND, LCD, SPI/CH374, Ethernet, ...>
    CheckPowerOnReason()
    display EBOOT banner and version string
    gpio_enable_alt(20)                 // PWM pad routing for backlight
    pwm_set(1000, 70)                   // 1 kHz, 70% duty backlight
    menu_return = <maintenance menu entry>
    select display mode based on menu_return (LCD vs TV Out)
    <touchpad init>
    return to eboot_main
```

The menu entry wrapper zeros a few bytes of menu state
(`0x801045F0..0x801045F6`) and then calls the maintenance menu
function. The maintenance menu is the function that handles the
"press F1 + type password + get the menu" flow. Its return code is
additionally used to select the display output: a specific return
value (5) switches EBOOT to TV Out instead of the on-board LCD.

The password authentication and the maintenance menu's full behavior
(menu items, format/update handlers, partition type mapping) are
documented in [maintenance-mode.md](maintenance-mode.md).
[usb-hid-input.md](usb-hid-input.md) describes the input-layer
hardware that feeds characters to this menu.

## `hw_phase1_init`

`hw_phase1_init` is the first hardware init step that runs out of
`oem_platform_init`. It has four jobs:

1. **Publish the SYSCTRL virtual base.** Calls `OALPAtoVA(0x08000000,
   0)` once and stores the result in the global at `0x80106E14`.
   Every subsequent GPIO and SYSCTRL access reads this global.

2. **`sysctrl_clock_init`.** Writes a large block of SYSCTRL
   registers to known-safe values. The writes are:

   ```
   GPIO1..4 direction = 0xFFFFFFFF     (all pins input)
   GPIO1..4 output    = 0
   GPIO1..4 int status (hypothesized +0xE0..+0xEC) = 0
   GPIO1..4 int mask   (hypothesized +0xF0..+0xFC) = 0xFFFFFFFF
   ```

   After this, EBOOT is in a clean-slate state: all GPIO pins are
   inputs, all outputs are zero, all interrupt sources cleared and
   fully masked. EBOOT itself never unmasks any interrupts, so the
   system runs in polling mode throughout.

3. **`hw_phase1_step2` and `hw_phase1_step3`.** Further early
   initialization stages; their per-function behavior is not
   documented in detail here.

4. **Eight mandatory alt-function enables.** Calls `gpio_enable_alt`
   with alt function IDs `44, 8, 53, 13, 12, 16, 51, 52` in that
   order. These eight alt functions are the minimum pin routing
   required by the drivers that come later (NAND controller, SPI,
   UART, LCD). They are enabled unconditionally regardless of boot
   path. The exact physical pin that each alt ID corresponds to is
   not mapped in this documentation - see the `Unresolved` entry in
   [gpio-driver.md](gpio-driver.md).

Driver-specific alt functions (such as ID `20` for the backlight PWM
pad and ID `51`/`52` re-enabled by `lcd_init`) are enabled later by
the respective drivers, not by `hw_phase1_init`.

## Driver Init Order

After `hw_phase1_init` returns, `oem_platform_init` runs the
driver-specific init functions in an order that respects the
mandatory-alt-function prerequisites. Observed order:

1. **NAND driver** (`nand_detect_device`). Probes the NAND chip via
   Read ID, looks up the device in the chip database, and sets the
   per-chip address byte counts. See [nand-driver.md](nand-driver.md).
2. **SPI0 / CH374** (via the maintenance menu entry, not during
   early init). EBOOT does not initialize CH374 unless it enters
   the maintenance menu path. The default boot flow never touches
   the HID keyboard.
3. **UART** (if present). On the test unit, the UART pads are
   damaged and the UART init completes but no characters are
   observable. EBOOT's UART driver path runs regardless.
4. **LCD** (`lcd_init`). Programs the LCD controller through the
   full bring-up sequence. See [lcd-driver.md](lcd-driver.md).
5. **Ethernet** (`eth_register_enc28j60`, which internally calls
   `enc28j60_init`). See [ethernet-driver.md](ethernet-driver.md).
   EBOOT also tries the Bulverde RNDIS USB Ethernet registration
   path; that init fails on AIPC and is a no-op.
6. **PTB load**. Reads the vendor partition table from NAND (or
   falls back to the compiled-in default). See
   [partition-format.md](partition-format.md).

Each driver init returns success/failure to `oem_platform_init`.
Failures in non-critical drivers (UART, Ethernet) do not stop the
boot.

## Maintenance Menu Entry

After the hardware is ready and the banner is displayed,
`oem_platform_init` enters the maintenance menu code path. The
menu's activation gate is:

1. EBOOT displays its banner and a short prompt.
2. The prompt waits for keyboard input for a bounded time.
3. If the user does nothing, the menu function times out and
   returns, and `oem_platform_init` falls through to the default
   boot path.
4. If the user presses **F1**, EBOOT prompts for a password.
5. The user types `Z T K Enter` (see
   [usb-hid-input.md](usb-hid-input.md) for the scan-code and
   hardware input path).
6. On a correct password, the maintenance menu is displayed and the
   user can select from its items.
7. On four incorrect password attempts, the function returns failure
   and `oem_platform_init` continues with the default boot path.

The specific return codes are:

| Return value | Meaning                                     |
| ------------ | ------------------------------------------- |
| 1            | F1 prompt timed out (user took no action)   |
| 2            | User pressed Escape to exit the menu        |
| 3            | Password entered incorrectly four times     |
| 5            | Menu action requested TV Out display mode   |
| other        | Menu action returning a generic value       |

Return value 5 is special: it causes `oem_platform_init` to set a
display-mode flag that triggers the TV Out path instead of the
on-board LCD. Other values fall through to the default LCD path.
This side-channel behavior (menu return code -> display mode) is
an unusual design choice and is noted so that a Linux port knows
not to rely on the same menu for both purposes.

## Default Boot Path: Load NK from NAND

On a clean boot with no maintenance action, EBOOT:

1. Reads the PTB from NAND and locates the `NK` entry.
2. Calls the `LoadNandBoot` loop with the `NK` partition's start
   block and block count, reading pages into DDR via the
   fresh-READ-per-chunk pattern (see
   [nand-driver.md](nand-driver.md)).
3. Loads the NK image to virtual `0x80200000` (physical
   `0x30200000`).
4. Jumps to the NK entry point. Control never returns to EBOOT.

The `NK` partition content is not a standard WinCE `NK.bin`; it is
an `ECEC` container with one or more sub-images. EBOOT loads the
raw partition bytes and relies on NK's own boot stub (present in
the first 64 bytes of the ECEC header) to set up whatever is needed
before the kernel's own `WinMain`/`NKStartup` runs. See
[partition-format.md](partition-format.md) for container details.

## Fallback: TFTP Download

If the maintenance menu selects a "download via Ethernet" option
(or if the NAND load fails), EBOOT enters the BOOTME/TFTP download
state machine. The state machine is fully documented in
[ethernet-driver.md](ethernet-driver.md). Summary:

1. EBOOT broadcasts BOOTME UDP packets on a fixed interval to
   announce itself to a listening Platform Builder on the local
   network.
2. The host sends a TFTP read request on port `0xD403` to the
   device IP (`192.168.0.11` by default).
3. EBOOT accepts the TFTP transfer, writes the received image to
   DDR.
4. The host sends an `EDBG_CMD_JUMPIMG` command, and EBOOT jumps
   to the loaded image.

The downloaded image typically replaces the NK in memory but does
not overwrite the NAND copy. To persist a downloaded NK, the
maintenance menu must be used to write it to the `NK` partition
separately.

## CheckPowerOnReason

`CheckPowerOnReason` runs early in `oem_platform_init`. It is the
only EBOOT code path that reads GPIO **inputs** for a purpose other
than default-state check. It:

1. Reads the power button pin (identified via a per-platform lookup
   table, not by a fixed constant) to distinguish a cold power-on
   from a warm reset.
2. Reads the charger-detect pin to determine whether external
   power is present.
3. Uses the results to decide whether to display a charging icon
   on the LCD or to boot straight through.

The per-pin configuration goes through `gpio_bank_config_write`
(setting the direction to input) followed by the GPIO aux writer
and then a read. Which of the two aux-write helpers is chosen
depends on a mode value from a per-platform configuration table,
using mode `9` to pick the second helper. See
[gpio-driver.md](gpio-driver.md) for the aux-write mechanism.

The exact pins used for power button and charger detect are looked
up at runtime via indirect helpers and are not fixed constants in
EBOOT. This means the same EBOOT image can in principle be used on
multiple board variants.

## Version Differences

This documentation targets firmware v1.88. An earlier v1.58.2 EBOOT
also exists on some units. The two versions share the same boot
flow structure; differences observed so far are scoped to
individual drivers and constants, not to the overall sequence. When
v1.58.2 diverges from v1.88 in a documented area, the relevant
driver document calls it out explicitly. The top-level boot flow
described here applies to both versions.

## Unresolved

- The maintenance menu body (full list of menu items, each item's
  handler, update paths to NAND, TFTP integration) is not
  decompiled.
- The precise division of work between `hw_phase1_init`,
  `hw_phase1_step2`, and `hw_phase1_step3` is not mapped; only the
  `hw_phase1_init` contents are known. Step 2 and step 3 run
  before the driver init phase but their individual register
  writes are not documented.
- The exact order in which the driver inits run after
  `hw_phase1_init` is inferred from code walk rather than directly
  verified; minor reordering may exist.
- `CheckPowerOnReason`'s power-button and charger-detect pin
  lookup tables are per-platform and come from a helper path that
  was not independently traced.
- The TV Out display mode entered on maintenance menu return
  value 5 uses a different set of LCD-controller writes and a
  different framebuffer layout. Those writes are not documented in
  this directory.
- The backlight PWM re-programming at `oem_platform_init` time uses
  the same `pwm_set(1000, 70)` call as `lcd_init`. Whether
  `lcd_init` also invokes it is consistent with both orderings and
  is not independently verified.
