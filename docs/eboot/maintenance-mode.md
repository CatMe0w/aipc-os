# Maintenance Mode

EBOOT provides a hidden maintenance menu gated behind a keyboard password.
The menu is the OEM's factory and field-service tool for reformatting NAND
partitions, reflashing the kernel or EBOOT itself, and rebooting the device.

See [usb-hid-input.md](usb-hid-input.md) for the hardware path (SPI0,
CH374, HID keyboard) that delivers keystrokes to this menu.

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

The Nand boot partition (which contains NBOOT and EBOOT) **is** formatted,
but only as a prerequisite step inside the "Update Eboot" handler (menu
item 5), not from the standalone "Format Nand disk" menu entry. The stub
may exist as a safety measure to prevent accidental bricking from the
menu.

### 2. Format XIP Disk

Calls `maint_format_partition(2)`, which internally dispatches to the
Flash Memory Device (FMD) layer with **partition type 4** (= XIP). This
formats the NK kernel partition. The formatter erases the partition's
NAND blocks and, depending on the partition flags, may create nested BINFS
and FAT sub-filesystems within it.

XIP stands for "Execute In Place", a WinCE partition type name used for
the kernel image region. On NAND-based systems the kernel is not truly
executed in place (it is loaded into DDR first), but the partition type
name persists from the NOR flash era.

### 3. Format Flash2 Disk

Calls `maint_format_partition(3)`, which dispatches to the FMD layer with
**partition type 5** (= IMGFS). IMGFS is the WinCE Image FileSystem used
for secondary storage. On AIPC this maps to the `DSK` partition in the
PTB.

There is no corresponding "Update Flash2" menu item; the IMGFS partition
can be formatted but not reflashed from this menu.

### 4. Update XIP

Handler: `maint_update_xip`.

1. Initialize the filesystem layer.
2. Open `\XIP.NB0` from the FAT filesystem.
3. Pre-format the XIP partition via `maint_format_partition(2)`.
4. Initialize the flash write path.
5. Read the file in 10 MB chunks (buffer at `0x80200000`, the NK load
   address) and write each chunk to the NAND XIP partition.
6. Display progress percentages as each chunk completes.

If `\XIP.NB0` does not exist on the filesystem, the handler prints
`"xip.nb0 not exist !"` and returns without modifying NAND.

The filesystem that holds `\XIP.NB0` is presumably the `DSK` FAT partition,
mounted by the filesystem init helper. The file could arrive there via a
prior TFTP download or via the USB mass storage path that WinCE exposes
when running NK.

### 5. Update Eboot

Handler: `maint_update_eboot`.

1. Initialize the filesystem layer.
2. Open `\EBOOT.NB0` from the FAT filesystem.
3. Pre-format the Nand boot partition via `maint_format_partition(1)`,
   which erases the partition (type 1) containing NBOOT and EBOOT.
4. Read 512 KB (`0x80000` bytes) from the file into DDR at uncached
   address `0xA0300000` (physical `0x30300000`).
5. Program the data to NAND.

If `\EBOOT.NB0` does not exist, the handler prints the error and returns.

Note: this handler formats the entire Nand boot partition (which also
contains NBOOT) before writing the new EBOOT. If the write fails after
the format, the device is bricked until a USB boot recovery is performed.

### 6. Reboot

Calls `system_reboot_watchdog`, a `__noreturn` function that triggers a
watchdog or software reset. A 500 ms delay precedes the call.

### ESC. Exit

Returns from the maintenance menu function. The return value is used by
the caller (`oem_platform_init`) to select the display mode: a specific
return value (5) triggers TV Out instead of the on-board LCD, while other
values continue with the default LCD path.

## Partition Type Mapping

The format dispatcher `maint_format_partition` translates its argument
through a switch into a WinCE partition type number:

| Argument | Partition type | WinCE name | PTB tag on AIPC |
| -------- | -------------- | ---------- | --------------- |
| 1        | 1              | (boot)     | NBT + IPL       |
| 2        | 4              | XIP        | NK              |
| 3        | 5              | IMGFS      | DSK             |

The full set of WinCE partition types, as listed in a format-time
configuration string inside EBOOT:

```
1.extended; 2.DOS32; 3.BINFS; 4.XIP; 5.IMGFS;
```

This enum is internal to the WinCE FMD layer and has no direct
relationship to the PTB entry tags documented in
[partition-format.md](partition-format.md). The mapping between the two
(XIP -> NK, IMGFS -> DSK, boot -> NBT+IPL) is hardcoded in the format
dispatcher.

## Format Internals

The underlying format function (`fmd_format_partition`, called by
`maint_format_partition`) performs:

1. `fmd_init()` - bring up the Flash Memory Device driver.
2. `fmd_get_partition_info(0, -1)` - read the partition table.
3. Erase the target partition's NAND blocks.
4. If the partition's flags include the `0x2000` bit, create a nested
   BINFS sub-partition (type 33) and a FAT sub-partition (type 4) within
   the erased region. This is the standard WinCE pattern for XIP
   partitions that contain both executable ROM modules (BINFS) and
   user-accessible files (FAT).

The FMD layer is a standard WinCE Flash Media Driver abstraction; its
internal details are not specific to AIPC and are not further documented
here.

## Unresolved

- The filesystem init helper and its mount path: `\XIP.NB0` and
  `\EBOOT.NB0` are read from a FAT filesystem, but which partition
  provides that filesystem (DSK? a RAM disk? a TFTP staging area?) is
  not traced beyond the `sub_80074830` init call.
- The "Format Nand disk" stub: whether this was intentionally disabled
  as a safety measure or is a build-time configuration artifact is not
  determined.
- The TV Out display mode (return value 5): the register writes and LCD
  controller reconfiguration for TV Out are not documented.
- `sub_80072A9C` called by `maint_update_dispatch` before updates: this
  function checks three device-status bytes at `0x80104671/83/95` and
  appears to tear down external devices (possibly USB ports via CH374)
  before writing to flash. Its behavior is not fully traced.
