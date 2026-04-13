# Maintenance Mode

EBOOT provides a hidden maintenance menu gated behind a keyboard password.
The menu is the OEM's factory and field-service tool for reformatting NAND
partitions, reflashing the kernel or EBOOT itself, and rebooting the device.

See [usb-hid-input.md](usb-hid-input.md) for the CH374-backed USB HID
path that delivers keystrokes to this menu.

## Activation

1. During EBOOT's early startup, the banner prints and EBOOT polls the HID
   keyboard for an **F1** keypress (HID usage code `0x3A`). The poll runs
   for 100 iterations with a 10 ms delay each, giving roughly a 1-second
   window.
2. If F1 is detected, EBOOT clears the LCD framebuffer and prints:
   `Please input password:`
3. The user types **Z T K** on the HID keyboard, then presses **Enter**.
   - The password is stored as HID usage codes, not ASCII:
     `0x1D` (Z), `0x17` (T), `0x0E` (K), `0x28` (Enter).
   - The reference password is hardcoded in the function prologue via four
     `MOV` + `STRB` instructions that place the bytes `{0x1D, 0x17, 0x0E}`
     into a 4-byte stack variable.
   - On Enter, `memcmp` compares the first 3 bytes of the input buffer
     against the reference. Enter itself is the trigger, not a compared
     byte.
   - Up to 4 attempts are allowed. On the 4th failure, the function
     returns and EBOOT continues with the default boot path.
4. On a successful password, the maintenance menu is displayed.
5. If no F1 is pressed within the timeout, the function returns `1` and
   EBOOT proceeds to boot NK normally.

Observed direct return values from `maintenance_menu` itself:

| Return value | Meaning                                               |
| ------------ | ----------------------------------------------------- |
| 1            | F1 prompt timed out                                   |
| 2            | Escape pressed at the password prompt                 |
| 3            | Password entered incorrectly four times               |
| 0x280E171D   | Escape pressed at the main menu (`ESC.Exit`)          |

`oem_platform_init` still compares the menu return value against `5` to
select TV Out, but the current `maintenance_menu` implementation has no
visible path that returns `5`.

## Menu Items

After a successful password, the following menu is displayed:

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

Each item is dispatched by matching the HID usage code of the pressed key
against a switch table. All destructive items prompt `Are you sure (y/n)?`
before proceeding (HID `0x1C` = y, `0x11` = n).

### 1. Format Nand Disk - **stubbed out**

Despite being listed in the menu, this item does **not** call any format
function. After the user confirms with `y`, EBOOT prints
`"Format Nand disk starting..."` and immediately returns to the menu
without erasing or reformatting anything.

This is verified from the disassembly: the `y` confirmation path loads the
"starting" string and branches directly to the common exit label with no
intervening `BL` to any format or erase function. By contrast, menu items
2 and 3 both call `maint_format_partition` before reaching the same exit
label.

The primary EBOOT partition **is** erased elsewhere, but only inside the
"Update Eboot" handler (menu item 5), not from the standalone
"Format Nand disk" menu entry. That update path targets PTB entry `1`
(`IPL`) and later mirrors the image to PTB entry `2` (`BAK`); it does
not directly format `NBT`. The stub may exist as a safety measure to
prevent accidental bootloader damage from the menu.

### 2. Format XIP Disk

Calls `maint_format_partition(2)`, which maps to `sub_800655D8(4)`.
In this build that numeric value resolves to PTB entry index `4`
(`NK`). The formatter erases that partition's NAND blocks and, depending
on the partition flags, may create nested BINFS and FAT sub-filesystems
within it.

XIP stands for "Execute In Place", a WinCE partition type name used for
the kernel image region. On NAND-based systems the kernel is not truly
executed in place (it is loaded into DDR first), but the partition type
name persists from the NOR flash era.

### 3. Format Flash2 Disk

Calls `maint_format_partition(3)`, which maps to `sub_800655D8(5)`.
In this build that numeric value resolves to PTB entry index `5`
(`DSK`). IMGFS is the WinCE Image FileSystem used for secondary
storage, and on AIPC this is the `DSK` partition in the PTB.

There is no corresponding "Update Flash2" menu item; the IMGFS partition
can be formatted but not reflashed from this menu.

### 4. Update XIP

Handler: `maint_update_xip`.

1. Initialize the file-system layer via `sub_80074830`.
2. Open `\XIP.NB0`.
3. Pre-format the XIP partition via `maint_format_partition(2)`.
4. Initialize the flash write path.
5. Read the file in 10 MB chunks (buffer at `0x80200000`, the NK load
   address) and write each chunk to the NAND XIP partition.
6. Display progress percentages as each chunk completes.

If `\XIP.NB0` does not exist, the handler prints
`"xip.nb0 not exist !"` and returns without modifying NAND.

### 5. Update Eboot

Handler: `maint_update_eboot`.

1. Initialize the file-system layer via `sub_80074830`.
2. Open `\EBOOT.NB0`.
3. Pre-format the primary EBOOT partition via `maint_format_partition(1)`,
   which passes `1` to `sub_800655D8` and therefore targets PTB entry
   index `1` (`IPL`), not `NBT`.
4. Request up to `0x80000` bytes from the file into DDR at uncached
   address `0xA0300000` (physical `0x30300000`).
5. Pass the actual returned byte count to `sub_80073550(1, size)`,
   which erases/programs `IPL` and, on success, erases/programs PTB
   entry `2` (`BAK`) with the same image.

If `\EBOOT.NB0` does not exist, the handler prints the error and returns.

Note: `NBT` is not rewritten by this handler. Failure after erasing
`IPL` and before successfully programming both `IPL` and `BAK` can still
leave the machine without a valid EBOOT image, but it is **not** the
same as wiping the first-stage `NBOOT` partition.

### 6. Reboot

Calls `system_reboot_watchdog`, a `__noreturn` function that triggers a
watchdog or software reset. A 500 ms delay precedes the call.

### ESC. Exit

At the main menu, `ESC` returns the 32-bit word `0x280E171D`, which is the
same stack word used to hold the hardcoded password bytes
`{0x1D, 0x17, 0x0E, 0x28}`.

This does **not** match the `5` that `oem_platform_init` checks for when
deciding whether to switch to TV Out. In the current build, `ESC.Exit`
therefore falls through to the normal LCD path.

## Format Target Mapping

The top-level format dispatcher `maint_format_partition` translates menu
selection `1/2/3` into the numeric arguments `1/4/5` and passes those
straight to `sub_800655D8`, which in turn calls `sub_80064B40(index)`.
In other words, at this layer the numbers are acting as **PTB entry
indices in this build**, not as a generic WinCE partition-type enum:

| Menu arg | `sub_800655D8` arg | PTB tag on AIPC | Observed role |
| -------- | ------------------ | --------------- | ------------- |
| 1        | 1                  | `IPL`           | primary EBOOT image |
| 2        | 4                  | `NK`            | kernel / XIP region |
| 3        | 5                  | `DSK`           | Flash2 / IMGFS storage |

The full set of WinCE partition types, as listed in a format-time
configuration string inside EBOOT:

```
1.extended; 2.DOS32; 3.BINFS; 4.XIP; 5.IMGFS;
```

This enum belongs to the deeper WinCE FMD helper layer. It is **not**
the same thing as the top-level `1/4/5` dispatch values above, even
though those later helpers do create child partitions of types such as
`33` (BINFS) and `4` (FAT) based on the selected PTB entry's flags.

## Format Internals

`maint_format_partition` itself does:

1. `fmd_init()`
2. `fmd_get_partition_info(0, -1)`
3. `sub_800672CC(...)`
4. Map menu argument `1/2/3` to PTB entry indices `1/4/5`
5. Call `sub_800655D8(index)`
6. If step 5 succeeds, call `sub_80066958()` to persist the updated PTB
   snapshot back into the `CFG` partition

Inside `sub_800655D8`, the verified flow is:

1. Look up the partition descriptor with `sub_80064B40(index)`.
2. Reject it if `flags & 3` is nonzero.
3. Call `sub_800654F8(part)` to erase the partition and, when
   `part->flags & 2`, mark all of its blocks reserved.
4. If `part->flags` has both `0x1000` and some nonzero bits in
   `0x00FF0000`, run the WinCE partition-format helper
   `sub_8007196C(...)`.
5. If `part->flags & 0x2000`, create child partitions of type `33`
   (BINFS) and type `4` (FAT) with `sub_80071DB8(...)`.
6. Refresh partition info with `fmd_get_partition_info(0, -1)`.

## Unresolved

- The filesystem init helper and its mount path: `\XIP.NB0` and
  `\EBOOT.NB0` are opened through the file-system layer, but which
  partition provides that filesystem is not traced beyond the
  `sub_80074830` init call.
- The "Format Nand disk" stub: whether this was intentionally disabled
  as a safety measure or is a build-time configuration artifact is not
  determined.
- `oem_platform_init` still tests for return value `5` to select TV Out,
  but the current `maintenance_menu` body has no visible path returning
  `5`. If a TV Out menu action existed, it is not present in this build.
- `sub_80072A9C` called by `maint_update_dispatch` before updates: this
  function checks three device-status bytes at `0x80104671/83/95` and
  appears to tear down external devices (possibly USB ports via CH374)
  before writing to flash. Its behavior is not fully traced.
