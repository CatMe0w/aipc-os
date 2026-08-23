# Boot Flow

This document describes the top-level execution path through EBOOT, from the handoff out of nboot to the jump into the WinCE NK kernel. It ties together the driver-level documents in the rest of this directory.

## Entry

nboot loads EBOOT into DDR. [docs/nboot/boot-flow.md](../nboot/boot-flow.md) documents the exact layout at entry. In short:

- nboot reads the first `0x64000` bytes of the `IPL` NAND partition into DDR, from physical `0x30037FD4`.
- The first `0x2C` bytes of that region are the `IMG` wrapper header. It describes EBOOT as a WinCE image.
- The first EBOOT instruction lives at physical `0x30038000`, virtual `0x80038000` after the MMU and the OEMAddressTable come up later.
- nboot branches to `0x30038000` in SVC mode, with SP already set up for EBOOT.

At physical `0x30038000`, the ARM assembly entry branches to the ARM relocation and setup routine. That routine does the minimum work to reach the C entry point.

## C Entry Point: `eboot_main`

The ARM entry prepares BSS, the stack and a preliminary MMU configuration, then calls `eboot_main`, the equivalent of a C `main()` for EBOOT. The high-level sequence inside `eboot_main`:

1. **BSS and `.data` initialization.** It copies the initialized `.data` from the read-only region of the image to the runtime addresses of `.data`, and zeroes BSS. This step fills the alt-function dispatch table at `0x800F0140`, the input-filter lookup table at `0x800F011C`, and every other compile-time table that the driver layer uses.

2. **`oem_early_init`.** It installs two OEM callback pointers and runs the early platform setup helper. After this, the rest of EBOOT uses `OALPAtoVA` freely. The address-table plumbing does not appear as one obvious "install OEMAddressTable here" block inside `oem_early_init`.

3. **`oem_platform_init`.** It runs the full hardware init sequence in the next section. On the common flash-boot path, the later `fmd_mount` boot menu can load and launch NK without a return to `eboot_main`.

4. **KITL and download continuation.** Only on the paths where `fmd_mount` returns control to `eboot_main`, EBOOT prints `"System ready!\r\nPreparing for download...\r\n"` and calls `check_update_eboot_request()`. That helper does the network-side BOOTME, TFTP and EDBG setup later in this document.

5. **Image handoff.** If a SimpleTFTP image stream is open, `nk_partition_load` parses `N000FF` and `B000FF` records from that stream. If the launch address is still zero at handoff time, `jump_to_nk_kernel` falls back to a load of the flash-resident NK image through `sub_80065F54(0x80200000, 0x400000)`.

6. **Never returns.** The final handoff to NK is terminal for EBOOT.

The final helper `eboot_handoff_to_launch_addr_mmu_off` is an image-handoff helper, not a watchdog setup routine. It takes the virtual launch address in `R0`, translates it into the matching physical address through the OEM address table, writes the CP15 control register value `0x70` to leave the MMU and cache state fit for the physical jump, invalidates the TLBs, and branches to the translated address. A value of `0x80200000` therefore jumps to physical `0x30200000`.

## `oem_platform_init`

`oem_platform_init` is the full platform bring-up function of EBOOT. It runs the init routine of every hardware driver in a fixed order, then enters the interactive menu loop. The top-level structure:

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

The menu entry wrapper zeroes four specific bytes of menu state, `0x801045F0`, `0x801045F1`, `0x801045F2` and `0x801045F6`, then calls the maintenance menu function. The maintenance menu handles the "press F1, type the password, get the menu" flow. `oem_platform_init` also tests its return code. A value of `5` would switch the caller to TV Out. In the current build no visible `maintenance_menu` path returns `5`, thus observed boots stay on LCD.

[maintenance-mode.md](maintenance-mode.md) documents the password authentication and the full behavior of the maintenance menu: the menu items, the format and update handlers, and the partition type mapping. [usb-hid-input.md](usb-hid-input.md) describes the input-layer hardware that feeds characters to this menu.

## `hw_phase1_init`

`hw_phase1_init` is the first hardware init step out of `oem_platform_init`. It has four jobs:

1. **Publish the SYSCTRL virtual base.** It calls `OALPAtoVA(0x08000000, 0)` once and stores the result in the global at `0x80106E14`. Every later GPIO and SYSCTRL access reads this global.

2. **`sysctrl_clock_init`.** It writes a large block of SYSCTRL registers to known-safe values:

   ```
   GPIO1..4 direction = 0xFFFFFFFF     (all pins input)
   GPIO1..4 output    = 0
   GPIO1..4 int status (hypothesized +0xE0..+0xEC) = 0
   GPIO1..4 int mask   (hypothesized +0xF0..+0xFC) = 0xFFFFFFFF
   ```

   EBOOT is then in a clean-slate state. All GPIO pins are inputs, all outputs are zero, and all interrupt sources are clear and fully masked. EBOOT itself never unmasks an interrupt, thus the system runs in polling mode throughout.

3. **`hw_phase1_step2` and `hw_phase1_step3`.** Further early initialization stages. This document does not cover their per-function behavior in detail.

4. **Eight mandatory alt-function enables.** It calls `gpio_enable_alt` with the alt function IDs `44, 8, 53, 13, 12, 16, 51, 52`, in that order. These eight alt functions are the minimum pin routing that the later drivers need: the NAND controller, SPI, UART and LCD. They go on in every boot path. This documentation does not map the pin behind each alt ID. See the `Unresolved` entry in [gpio-driver.md](gpio-driver.md).

The respective drivers enable their own alt functions later, not `hw_phase1_init`. Examples are ID `20` for the backlight PWM pad, and ID `51`, which `lcd_init` enables again.

## Driver Init Order

After `hw_phase1_init` returns, `oem_platform_init` does this:

1. **Power-on-reason setup** (`power_on_reason_init`). It reads the power-on reason, configures the keep-power-on GPIO path, drives GPIO pin `104` high, and stores the reason in bootargs.
2. **Panel GPIO preset**. It looks up the panel reset pin and the panel power pin, and drives both low. On v1.88 these helpers return pin `69` and pin `4`.
3. **LCD bring-up** (`lcd_init`). It programs the LCD controller.
4. **Framebuffer console setup**. It clears 5 MB at `0x87B00000`, initializes the console framebuffer parameters, and prints the version and banner strings.
5. **Touchpad init**. It calls `touchpad_init_1`, samples one keycode, then calls `touchpad_init_3`.
6. **Backlight enable**. It clears `0x74` bytes at `0xA0020800`, delays `0x55` ms, enables alt ID `20`, and calls `pwm_set(1000, 70)`.
7. **Maintenance menu**. It calls `maintenance_menu_entry`. The caller still checks for the return value `5` as a TV-Out selector, but no visible path in the current `maintenance_menu` implementation returns that value.
8. **FMD and boot configuration**. It initializes the flash layer, queries the partition info, and enters the boot countdown and config logic of `fmd_mount`. Only then, and only on the subset of paths that stay in EBOOT, does it return to `eboot_main`.

## Maintenance Menu Entry

After the hardware is ready and the banner is on screen, `oem_platform_init` enters the maintenance menu code path. The activation gate of the menu is:

1. EBOOT displays its banner and a short prompt.
2. The prompt waits for keyboard input for a bounded time.
3. If the user does nothing, the menu function times out and returns, and `oem_platform_init` falls through to the default boot path.
4. If the user presses **F1**, EBOOT prompts for a password.
5. The user types `Z T K Enter`. See [usb-hid-input.md](usb-hid-input.md) for the scan-code and the hardware input path.
6. On a correct password, the maintenance menu appears and the user can select an item.
7. After four incorrect password attempts, the function returns failure and `oem_platform_init` continues on the default boot path.

The return codes are:

| Return value | Meaning                                 |
| ------------ | --------------------------------------- |
| 1            | F1 prompt timed out                     |
| 2            | Escape pressed at the password prompt   |
| 3            | Password entered incorrectly four times |
| 0x280E171D   | Escape pressed at the main menu         |

`oem_platform_init` still compares the menu return value against `5` to select TV Out, but the current `maintenance_menu` implementation has no visible path that returns `5`. In the current build, every observed maintenance-menu return falls through to the default LCD path.

## FMD Boot Menu and Default Target

After the maintenance menu and the framebuffer setup, `oem_platform_init` calls `fmd_mount()`. Despite the name, this is not a passive "mount and return" helper. It prints a boot menu, reads the PTB boot-delay byte (`+0x1E`) and the default-boot field (`+0x24`), and polls for user input during the countdown. It can then boot immediately, enter a configuration menu, or return to `eboot_main` for KITL and download operation.

The verified user-visible boot menu is:

```
==========Boot Menu==========
[b]Boot Kernel.
[u]Run Updata Loader.
[Enter]Kitl boot.
[Space]Config.
```

The factory-default configuration is `boot delay = 0` and `default boot target = 4` (`NK`). With a boot delay of zero, the countdown loop does not run at all. The timeout path executes at once and boots NK from flash with no pause.

At the key-dispatch level, `fmd_mount` does this:

- `[b]` searches the PTB for tag `NK` and hands off through `sub_80067120(...)`.
- `[u]` searches the PTB for tag `UDR` and hands off through the same helper. This is the actual control flow behind the UI string `"Run Updata Loader"`.
- `[Enter]` returns immediately, thus `eboot_main` continues into `check_update_eboot_request()`.
- `[Space]` enters the config submenu, then returns to the caller, which again leaves `eboot_main` in control.

PTB default target `9` means "menu", and PTB default target `10` means "KITL". These are the two cases that leave EBOOT in control and let the later `check_update_eboot_request()` network path run.

### Keyboard Input Stub

The Boot Menu and every submenu, Main Menu, Base Config, Manage Partition and the rest, use `sub_80063B0C` for keyboard input. In this build of EBOOT, `sub_80063B0C` is a fixed stub:

```asm
MOV R0, #0
BX  LR
```

It always returns `0`, which means that no key press ever arrives. This has two consequences:

1. **The Boot Menu and every config menu are non-interactive.** The menu text goes to the framebuffer and the countdown decrements normally, but no key produces a response: not Space, not Enter, not b or u, and not any numbered option. After the countdown expires, the timeout path executes the default boot target.

2. **The maintenance menu is the only keyboard-interactive path.** The F1 upgrade flow (`maintenance_menu@0x8007079c`) reads keys directly through `ch374_poll_hid_keycode`. That is a separate code path, it does talk to the USB HID keyboard hardware, and it works.

A non-zero `boot delay`, written to PTB byte `+0x1E` in NAND, makes the countdown visible. The menu stays non-interactive for the same reason.

### Main Menu (Config Submenu)

`[Space]` in the Boot Menu would enter the Main Menu, in `sub_80064654@0x80064654`. In practice that needs default boot target `9` and a working key input path. The menu contents are:

```
==========Main Menu==========
[1]Base Config.            # IP, subnet, DHCP, boot delay, default boot, KITL transport
[2]Manage Partition.       # view, create, format, erase partitions
[3]Manual Upgrade.         # upgrade individual partitions or NK via KITL
[d]Download run-time image & run.     # TFTP/KITL download then execute
[k]Download run-time image & write to flash.
[b]Boot image now.         # select partition and boot from NAND
```

The Base Config submenu (`sub_80063F34@0x80063F34`) exposes seven settings:

- `[1]` IP address (default `192.168.0.11`)
- `[2]` Subnet mask (default `255.255.255.0`)
- `[4]` DHCP toggle (default `Disabled`)
- `[5]` Boot Delay in seconds (default `0`, range `1..255`)
- `[6]` Default Boot target (`NK`, `Menu` or `kitl`)
- `[7]` KITL transport (`AKUSB` or `ENC28J60`)
- `[r]` Reset to factory defaults, `[s]` Save changes, `[e]` Return

## Flash Boot Path: PTB Target `NK`

On the stock flash-boot path, `default boot target = 4`, EBOOT does **not** call `LoadNandBoot` from `eboot_main`. It does this instead:

1. `fmd_mount` resolves PTB entry index `4` (`NK`) and calls `sub_80067120(4)`.
2. `sub_80067120(4)` calls `sub_80065F54(0x80200000, 0x400000)`, which opens the flash-backed kernel image through the WinCE partition layer and reads it into RAM.
3. `sub_80065F54` reads the first `68` bytes, requires `ECEC` at offset `+0x40`, and then reads the rest of the image.
4. Control passes through the reboot and launch helper, with the load address from the PTB entry.

The content of the `NK` partition is not a standard WinCE `NK.bin`. It is an `ECEC` container with one or more sub-images. EBOOT loads the raw partition bytes and relies on the boot stub of NK itself, in the first 64 bytes of the ECEC header, to set up whatever the kernel needs before its own `WinMain` or `NKStartup` runs. See [partition-format.md](partition-format.md) for the container details.

`LoadNandBoot` is a different helper for raw boot-image reads and upgrade verification. [nand-driver.md](nand-driver.md) documents it, but it is **not** the normal `NK` flash-boot path.

## KITL / TFTP Path

When `fmd_mount` returns to `eboot_main` in KITL or download mode, `eboot_main` calls `check_update_eboot_request()`. That helper:

1. Copies either the static PTB IP and mask, or the DHCP-zeroed placeholders, into the runtime network state. PTB boot flag bit `1` selects which.
2. Registers the SimpleTFTP server on UDP port `0xD403`.
3. Runs `EbootSendBootmeAndWaitForTftp`, which sends BOOTME packets and waits for the host to open the TFTP transfer.

Once the host opens the transfer, `check_update_eboot_request()` returns `0`, and `eboot_main` calls `nk_partition_load` at once. Despite the historic name, this function is the **download-stream parser**. It reads from the already-open SimpleTFTP source through `sub_8005A4AC` and `sub_8005BFBC`, understands `N000FF` and `B000FF` records, and rejects `X000FF` outright.

If an `EDBG_CMD_JUMPIMG` command already filled the launch-state globals, `check_update_eboot_request()` returns `1` instead. `eboot_main` then skips `nk_partition_load` and goes straight to `jump_to_nk_kernel`.

[ethernet-driver.md](ethernet-driver.md) documents the BOOTME, TFTP and EDBG packet handling itself.

## `power_on_reason_init`

`power_on_reason_init` runs immediately after `hw_phase1_init`. From the assembly:

1. Read a power-on reason code from a helper path, and print one of `REASON_PWRBTN`, `REASON_USB`, `REASON_CHARGER`, `REASON_ALARM`, `REASON_NONE`, or a raw decimal reason value.
2. Look up a board-specific "KeepPowerOn" pin. If it exists, configure it through `gpio_bank_config_write`, one aux-config helper, and `gpio_bank_data_write`.
3. Drive GPIO pin `104` high in every case, through `gpio_set_value(104, 1)`.
4. Store the final power-on reason code to bootargs at `0xA002084C`.

The keep-power-on GPIO is board-specific, and the lookup is indirect. The fixed pin `104` write is present in the verified v1.88 path.

## Version Differences

This documentation targets firmware v1.88. An earlier v1.58.2 EBOOT also exists on some units. The two versions share the same boot flow structure. The differences so far stay inside individual drivers and constants, and none of them changes the overall sequence. Where v1.58.2 differs from v1.88 in a documented area, the relevant driver document says so. The top-level boot flow here applies to both versions.

## Unresolved

- The exact division of work between `hw_phase1_init`, `hw_phase1_step2` and `hw_phase1_step3`. Only the contents of `hw_phase1_init` are known. Step 2 and step 3 run before the driver init phase, but this document does not cover their individual register writes.
- The helper path of `power_on_reason_init` that decodes the reason code and looks up the keep-power-on GPIO is only partly traced.
- The exact naming of the KITL and download handoff globals (`0x800F5110`, `0x800F36C0`, and the related launch-state fields) is still incomplete, even though the control flow around them is verified.
- The TV Out display mode, which the maintenance menu return value 5 selects, uses a different set of LCD-controller writes and a different framebuffer layout. This directory does not document those writes.
