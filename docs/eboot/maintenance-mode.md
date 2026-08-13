# Maintenance Mode

EBOOT holds a hidden maintenance menu behind a keyboard password. The menu is the factory and field-service tool of the OEM. It reformats NAND partitions, reflashes the kernel or EBOOT itself, and reboots the device.

See [usb-hid-input.md](usb-hid-input.md) for the CH374-backed USB HID path that delivers the keystrokes to this menu.

## Activation

1. During the early startup of EBOOT, the banner prints and EBOOT polls the HID keyboard for an **F1** keypress, HID usage code `0x3A`. The poll runs 100 times with a 10 ms delay each, which gives a window of about 1 second.
2. After F1, EBOOT clears the LCD framebuffer and prints `Please input password:`.
3. The user types "ztk" on the HID keyboard, then presses **Enter**.
   - The password consists of HID usage codes, not ASCII: `0x1D` (Z), `0x17` (T), `0x0E` (K), `0x28` (Enter).
   - The reference password is hardcoded in the function prologue, in four `MOV` and `STRB` instructions that put the bytes `{0x1D, 0x17, 0x0E}` into a 4-byte stack variable.
   - On Enter, `memcmp` compares the first 3 bytes of the input buffer against the reference. Enter itself is the trigger, not a compared byte.
   - The gate allows up to 4 attempts. On the 4th failure the function returns, and EBOOT continues on the default boot path.
4. After a correct password, the maintenance menu appears.
5. If no F1 arrives inside the timeout, the function returns `1` and EBOOT boots NK normally.

Observed direct return values from `maintenance_menu` itself:

| Return value | Meaning                                      |
| ------------ | -------------------------------------------- |
| 1            | F1 prompt timed out                          |
| 2            | Escape pressed at the password prompt        |
| 3            | Password entered incorrectly four times      |
| 0x280E171D   | Escape pressed at the main menu (`ESC.Exit`) |

`oem_platform_init` still compares the menu return value against `5` to select TV Out, but the current `maintenance_menu` implementation has no visible path that returns `5`.

## Menu Items

After a correct password, this menu appears:

```
 System upgrade:
 1.Format Nand disk
 2.Format XIP disk
 3.Format Flash2 disk
 4.Update XIP
 5.Update Eboot
 6.Reboot
 ESC.Exit

Waitting for press key
```

A switch table matches the HID usage code of the pressed key and dispatches each item. Every destructive item prompts `Are you sure (y/n)?` first, where HID `0x1C` is y and `0x11` is n.

### 1. Format Nand Disk - **stubbed out**

The menu lists this item, but the item calls no format function. After the user confirms with `y`, EBOOT prints `"Format Nand disk starting..."` and returns to the menu at once. It erases nothing and reformats nothing.

The disassembly verifies this. The `y` confirmation path loads the "starting" string and branches straight to the common exit label, with no `BL` to any format or erase function in between. Menu items 2 and 3 both call `maint_format_partition` before they reach the same exit label.

Something else does erase the primary EBOOT partition, but only inside the "Update Eboot" handler, menu item 5, and not from the standalone "Format Nand disk" entry. That update path targets PTB entry `1` (`IPL`) and later mirrors the image to PTB entry `2` (`BAK`). It does not format `NBT` directly. The stub can exist as a safety measure, to prevent accidental bootloader damage from the menu.

### 2. Format XIP Disk

This item calls `maint_format_partition(2)`, which maps to `sub_800655D8(4)`. In this build that numeric value resolves to PTB entry index `4` (`NK`). The formatter erases the NAND blocks of that partition. Depending on the partition flags, it can also create nested BINFS and FAT sub-filesystems inside it.

XIP means "Execute In Place", a WinCE partition type name for the kernel image region. On a NAND-based system the kernel does not truly execute in place, because it loads into DDR first, but the partition type name stays from the NOR flash era.

### 3. Format Flash2 Disk

This item calls `maint_format_partition(3)`, which maps to `sub_800655D8(5)`. In this build that numeric value resolves to PTB entry index `5` (`DSK`). IMGFS is the WinCE Image FileSystem for secondary storage, and on the AIPC that is the `DSK` partition in the PTB.

There is no matching "Update Flash2" menu item. This menu can format the IMGFS partition but cannot reflash it.

### 4. Update XIP

Handler: `maint_update_xip`.

1. Initialize the file-system layer with `sub_80074830`.
2. Open `\XIP.NB0`.
3. Pre-format the XIP partition with `maint_format_partition(2)`.
4. Initialize the flash write path.
5. Read the file in 10 MB chunks, into the buffer at `0x80200000`, the NK load address, and write each chunk to the NAND XIP partition.
6. Show a progress percentage as each chunk completes.

If `\XIP.NB0` does not exist, the handler prints `"xip.nb0 not exist !"` and returns with no change to NAND.

### 5. Update Eboot

Handler: `maint_update_eboot`.

1. Initialize the file-system layer with `sub_80074830`.
2. Open `\EBOOT.NB0`.
3. Pre-format the primary EBOOT partition with `maint_format_partition(1)`. That passes `1` to `sub_800655D8` and therefore targets PTB entry index `1` (`IPL`), not `NBT`.
4. Request up to `0x80000` bytes from the file into DDR at uncached address `0xA0300000`, physical `0x30300000`.
5. Pass the actual returned byte count to `sub_80073550(1, size)`. That call erases and programs `IPL`, and on success it erases and programs PTB entry `2` (`BAK`) with the same image.

If `\EBOOT.NB0` does not exist, the handler prints the error and returns.

This handler does not rewrite `NBT`. A failure after the erase of `IPL`, and before a successful program of both `IPL` and `BAK`, can still leave the machine with no valid EBOOT image. That is **not** the same as a wipe of the first-stage `NBOOT` partition.

### 6. Reboot

This item prints `"Reboot\r\n"`, waits 500 ms, then calls `eboot_handoff_to_launch_addr_mmu_off` with the launch address `0x80200000`. Despite the legacy name `system_reboot_watchdog` in older notes, the helper programs no watchdog. It translates the launch address into a physical address, disables the MMU and cache state for the handoff, invalidates the TLBs, and jumps to the translated address.

### ESC. Exit

At the main menu, `ESC` returns the 32-bit word `0x280E171D`. This is the same stack word that holds the hardcoded password bytes `{0x1D, 0x17, 0x0E, 0x28}`.

It does **not** match the `5` that `oem_platform_init` checks for the switch to TV Out. In the current build, `ESC.Exit` therefore falls through to the normal LCD path.

## Format Target Mapping

The top-level format dispatcher `maint_format_partition` translates the menu selection `1/2/3` into the numeric arguments `1/4/5`, and passes those straight to `sub_800655D8`, which calls `sub_80064B40(index)`. At this layer the numbers act as **PTB entry indices in this build**, not as a generic WinCE partition-type enum:

| Menu arg | `sub_800655D8` arg | PTB tag on AIPC | Observed role          |
| -------- | ------------------ | --------------- | ---------------------- |
| 1        | 1                  | `IPL`           | primary EBOOT image    |
| 2        | 4                  | `NK`            | kernel / XIP region    |
| 3        | 5                  | `DSK`           | Flash2 / IMGFS storage |

The full set of WinCE partition types, from a format-time configuration string inside EBOOT:

```
1.extended; 2.DOS32; 3.BINFS; 4.XIP; 5.IMGFS;
```

This enum belongs to the deeper WinCE FMD helper layer. It is **not** the same thing as the top-level `1/4/5` dispatch values above, even though those later helpers do create child partitions of types such as `33` (BINFS) and `4` (FAT), from the flags of the selected PTB entry.

## Format Internals

`maint_format_partition` itself does this:

1. `fmd_init()`
2. `fmd_get_partition_info(0, -1)`
3. `sub_800672CC(...)`
4. Map the menu argument `1/2/3` to the PTB entry index `1/4/5`
5. Call `sub_800655D8(index)`
6. If step 5 succeeds, call `sub_80066958()` to persist the updated PTB snapshot back into the `CFG` partition

Inside `sub_800655D8`, the verified flow is:

1. Look up the partition descriptor with `sub_80064B40(index)`.
2. Reject it if `flags & 3` is non-zero.
3. Call `sub_800654F8(part)` to erase the partition. With `part->flags & 2`, mark all of its blocks reserved.
4. If `part->flags` has both `0x1000` and some non-zero bits in `0x00FF0000`, run the WinCE partition-format helper `sub_8007196C(...)`.
5. If `part->flags & 0x2000`, create child partitions of type `33` (BINFS) and type `4` (FAT) with `sub_80071DB8(...)`.
6. Refresh the partition info with `fmd_get_partition_info(0, -1)`.

## Unresolved

- The filesystem init helper and its mount path. `\XIP.NB0` and `\EBOOT.NB0` open through the file-system layer, but the trace does not show which partition supplies that filesystem beyond the `sub_80074830` init call.
- The "Format Nand disk" stub. Whether someone disabled it deliberately as a safety measure, or whether it is a build-time configuration artifact, is not determined.
- `oem_platform_init` still tests for the return value `5` to select TV Out, but the current `maintenance_menu` body has no visible path that returns `5`. If a TV Out menu action once existed, this build does not have it.
- `sub_80072A9C`, which `maint_update_dispatch` calls before an update. This function checks three device-status bytes at `0x80104671`, `0x80104683` and `0x80104695`, and it appears to tear down external devices before a write to flash, possibly the USB ports through CH374. Its behavior is not fully traced.
