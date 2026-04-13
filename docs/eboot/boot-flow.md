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
   lookup table at `0x800F011C`, and all the other compile-time
   tables referenced from across the driver layer.

2. **`oem_early_init`.** Installs two OEM callback pointers and runs
   the early platform setup helper. After this point the rest of EBOOT
   freely uses `OALPAtoVA`, but the address-table plumbing is not
   materialized as a single obvious "install OEMAddressTable here"
   block inside `oem_early_init` itself.

3. **`oem_platform_init`.** Runs the full hardware init sequence
   described in the next section. On the common flash-boot path, the
   later `fmd_mount` boot menu can load and launch NK without ever
   returning to `eboot_main`.

4. **KITL / download continuation.** Only on paths where `fmd_mount`
   returns control to `eboot_main`, EBOOT prints
   `"System ready!\r\nPreparing for download...\r\n"` and calls
   `check_update_eboot_request()`. That helper performs the network-side
   BOOTME / TFTP / EDBG setup described later in this document.

5. **Image handoff.** If a SimpleTFTP image stream has been opened,
   `nk_partition_load` parses `N000FF` / `B000FF` records from that
   stream. If the launch address is still zero at handoff time,
   `jump_to_nk_kernel` falls back to loading the flash-resident NK
   image via `sub_80065F54(0x80200000, 0x400000)`.

6. **Never returns.** The final handoff to NK is terminal from EBOOT's
   point of view.

## `oem_platform_init`

`oem_platform_init` is EBOOT's full platform bring-up function. It
runs all hardware drivers' init routines in a fixed order and then
enters the interactive menu loop. The top-level structure:

```
oem_platform_init():
    hw_phase1_init()
    power_on_reason_init()
    gpio_set_value(get_lcd_panel_reset_pin(), 0)   // pin 69 on v1.88
    gpio_set_value(get_lcd_panel_power_pin(), 0)   // pin 4 on v1.88
    lcd_init()
    fb_clear_5mb()
    console_init_fb_params(0x87B00000)
    display EBOOT banner and version strings
    touchpad_init_1()
    touchpad_get_keycode()
    touchpad_init_3()
    memset(0xA0020800, 0, 0x74)
    delay_ms_alt(0x55)
    gpio_enable_alt(20)                 // PWM pad routing for backlight
    pwm_set(1000, 70)                   // 1 kHz, 70% duty backlight
    menu_return = maintenance_menu_entry()
    select display mode based on menu_return (LCD vs TV Out)
    fb_clear_5mb()
    print boot banner spacing and final init message
    fmd_init()
    fmd_get_partition_info(0, 0xFFFFFFFF)
    boot_path = fmd_mount()             // countdown, default boot target, config/KITL menus
    if boot_path returns:
        fmd_read_partition_table()
        return to eboot_main
```

The menu entry wrapper zeroes four specific bytes of menu state
(`0x801045F0`, `0x801045F1`, `0x801045F2`, `0x801045F6`) and then calls
the maintenance menu function. The maintenance menu is the function that handles the
"press F1 + type password + get the menu" flow. Its return code is
additionally tested by `oem_platform_init`: if it were `5`, the caller
would switch to TV Out. In the current build no visible
`maintenance_menu` path returns `5`, so observed boots remain on LCD.

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
pad and ID `51` re-enabled by `lcd_init`) are enabled later by
the respective drivers, not by `hw_phase1_init`.

## Driver Init Order

After `hw_phase1_init` returns, `oem_platform_init` performs:

1. **Power-on-reason setup** (`power_on_reason_init`). Reads the
   power-on reason, configures the keep-power-on GPIO path, drives
   GPIO pin `104` high, and stores the reason in bootargs.
2. **Panel GPIO preset**. Looks up the panel reset pin and panel
   power pin and drives them low. On v1.88 these helpers return pin
   `69` and pin `4` respectively.
3. **LCD bring-up** (`lcd_init`). Programs the LCD controller.
4. **Framebuffer console setup**. Clears 5 MB at `0x87B00000`,
   initializes console framebuffer parameters, and prints the
   version/banner strings.
5. **Touchpad init**. Calls `touchpad_init_1`, samples one keycode,
   then calls `touchpad_init_3`.
6. **Backlight enable**. Clears `0x74` bytes at `0xA0020800`,
   delays `0x55` ms, enables alt ID `20`, and calls
   `pwm_set(1000, 70)`.
7. **Maintenance menu**. Calls `maintenance_menu_entry`; the caller
   still checks for return value `5` as a TV-Out selector, but no
   visible path in the current `maintenance_menu` implementation
   returns that value.
8. **FMD / boot configuration**. Initializes the flash layer, queries
   partition info, enters `fmd_mount`'s boot countdown / config logic,
   and only then returns to `eboot_main` on the subset of paths that
   stay in EBOOT.

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
| 1            | F1 prompt timed out                         |
| 2            | Escape pressed at the password prompt       |
| 3            | Password entered incorrectly four times     |
| 0x280E171D   | Escape pressed at the main menu             |

`oem_platform_init` still compares the menu return value against `5`
to select TV Out, but the current `maintenance_menu` implementation
has no visible path that returns `5`. In the current build, all
observed maintenance-menu returns fall through to the default LCD
path.

## FMD Boot Menu and Default Target

After the maintenance menu and framebuffer setup, `oem_platform_init`
calls `fmd_mount()`. Despite the name, this is not a passive "mount and
return" helper: it prints a boot menu, reads the PTB boot-delay byte
(`+0x1E`) and default-boot field (`+0x24`), polls for user input during
the countdown, and can either boot immediately, enter configuration
menus, or return to `eboot_main` for KITL / download operation.

The verified user-visible boot menu is:

```text
==========Boot Menu==========
[b]Boot Kernel.
[u]Run Updata Loader.
[Enter]Kitl boot.
[Space]Config.
```

At the key-dispatch level, `fmd_mount` does the following:

- `[b]` searches the PTB for tag `NK` and hands off through
  `sub_80067120(...)`.
- `[u]` searches the PTB for tag `UDR` and hands off through the same
  helper; this is the actual control-flow behind the UI string
  `"Run Updata Loader"`.
- `[Enter]` returns immediately so `eboot_main` continues into
  `check_update_eboot_request()`.
- `[Space]` enters the config submenu and then returns to the caller,
  again leaving `eboot_main` in control afterwards.

The common default configuration is `boot target = 4`, which means
`NK`. In that case, once the countdown expires, `fmd_mount` does not
return to `eboot_main`: it resolves PTB entry index `4` and hands off
through `sub_80067120(4)` instead.

PTB default target `9` means "menu", and PTB default target `10` means
"KITL". Those are the two cases that leave EBOOT in control and allow
the later `check_update_eboot_request()` network path to run.

## Flash Boot Path: PTB Target `NK`

On the stock flash-boot path (`default boot target = 4`), EBOOT does
**not** call `LoadNandBoot` from `eboot_main`. Instead:

1. `fmd_mount` resolves PTB entry index `4` (`NK`) and calls
   `sub_80067120(4)`.
2. `sub_80067120(4)` invokes `sub_80065F54(0x80200000, 0x400000)`,
   which opens the flash-backed kernel image through the WinCE partition
   layer and reads it into RAM.
3. `sub_80065F54` reads the first `68` bytes, requires `ECEC` at
   offset `+0x40`, and then reads the remainder of the image.
4. Control transfers through the reboot / launch helper using the
   PTB entry's load address.

The `NK` partition content is not a standard WinCE `NK.bin`; it is
an `ECEC` container with one or more sub-images. EBOOT loads the
raw partition bytes and relies on NK's own boot stub (present in
the first 64 bytes of the ECEC header) to set up whatever is needed
before the kernel's own `WinMain`/`NKStartup` runs. See
[partition-format.md](partition-format.md) for container details.

`LoadNandBoot` is a different helper used for raw boot-image reads and
upgrade verification; it is documented in [nand-driver.md](nand-driver.md)
but it is **not** the normal `NK` flash-boot path.

## KITL / TFTP Path

When `fmd_mount` returns to `eboot_main` in KITL / download mode,
`eboot_main` calls `check_update_eboot_request()`. That helper:

1. Copies either the static PTB IP/mask or the DHCP-zeroed placeholders
   into the runtime network state, depending on PTB boot flag bit `1`.
2. Registers the SimpleTFTP server on UDP port `0xD403`.
3. Runs `EbootSendBootmeAndWaitForTftp`, which sends BOOTME packets and
   waits for the host to open the TFTP transfer.

Once the host has opened the transfer, `check_update_eboot_request()`
returns `0`, and `eboot_main` immediately calls `nk_partition_load`.
Despite the historic name, this function is the **download-stream
parser**: it reads from `sub_8005A4AC -> sub_8005BFBC` (the
already-open SimpleTFTP source), understands `N000FF` / `B000FF`
records, and explicitly rejects `X000FF`.

If an `EDBG_CMD_JUMPIMG` command has already populated the launch-state
globals, `check_update_eboot_request()` returns `1` instead, and
`eboot_main` skips `nk_partition_load` and goes straight to
`jump_to_nk_kernel`.

The BOOTME / TFTP / EDBG packet handling itself is documented in
[ethernet-driver.md](ethernet-driver.md).

## `power_on_reason_init`

`power_on_reason_init` runs immediately after `hw_phase1_init`. From
assembly:

1. Read a power-on reason code from a helper path and print one of:
   `REASON_PWRBTN`, `REASON_USB`, `REASON_CHARGER`, `REASON_ALARM`,
   `REASON_NONE`, or a raw decimal reason value.
2. Look up a board-specific "KeepPowerOn" pin. If present, configure
   it through `gpio_bank_config_write`, one aux-config helper, and
   `gpio_bank_data_write`.
3. Drive GPIO pin `104` high unconditionally via `gpio_set_value(104, 1)`.
4. Store the final power-on reason code to bootargs at `0xA002084C`.

The keep-power-on GPIO is board-specific and looked up indirectly. The
fixed pin `104` write is present in the verified v1.88 path.

## Version Differences

This documentation targets firmware v1.88. An earlier v1.58.2 EBOOT
also exists on some units. The two versions share the same boot
flow structure; differences observed so far are scoped to
individual drivers and constants, not to the overall sequence. When
v1.58.2 diverges from v1.88 in a documented area, the relevant
driver document calls it out explicitly. The top-level boot flow
described here applies to both versions.

## Unresolved

- The precise division of work between `hw_phase1_init`,
  `hw_phase1_step2`, and `hw_phase1_step3` is not mapped; only the
  `hw_phase1_init` contents are known. Step 2 and step 3 run
  before the driver init phase but their individual register
  writes are not documented.
- `power_on_reason_init`'s helper path for decoding the reason code
  and looking up the keep-power-on GPIO is only partially traced.
- The exact semantic naming of the KITL / download handoff globals
  (`0x800F5110`, `0x800F36C0`, related launch-state fields) is still
  incomplete, even though the control-flow around them is verified.
- The TV Out display mode entered on maintenance menu return
  value 5 uses a different set of LCD-controller writes and a
  different framebuffer layout. Those writes are not documented in
  this directory.
