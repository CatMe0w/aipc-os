# Display Driver

The WinCE display driver in `NK.ecec_01` is `anykaDisp.dll`. It takes the LCD controller over from EBOOT, allocates its own framebuffer and reprograms the base address.

[docs/soc/lcd.md](../soc/lcd.md) has the controller itself.

`mpulcd.dll` holds the panel timing. It exports `Get_RgbParam` and `Get_LcdParam`, and `anykaDisp.dll` imports both. For an RGB panel such as the one on this board, every timing number the controller receives comes from a table inside that DLL.

## Registry Configuration

`anykaDisp.dll` builds its display state from `HKLM\Drivers\Display\anykaPrimary\CONFIG`. That is the only display instance on this device.

| Registry value | Display context offset | Value on this device |
| --- | --: | --- |
| `mpulcdtype` | `+0xF0` | `0x100` |
| `bpp` | `+0x7C` | 16 |
| `width` | `+0x08` | 240 |
| `height` | `+0x0C` | 320 |
| `LCDPhysicalFrameBase` | `+0x44` | `0x33B00000` |
| `LCDVirtualFrameBase` | `+0xD4` | `0xA3B00000` |
| `FrameBufferSize` | `+0x104` | `0x500000` |
| `refreshintervalstatic` | `+0xF8` | 100 |
| `refreshintervalvideo` | `+0xFC` | 40 |
| `HWCursor` | `+0x48` | 1 |

When `mpulcdtype` is `0x100` or above, the driver ignores `width` and `height` and takes the active size from the panel helper. Thus the 240 by 320 in the registry has no effect on this device. The driver uses `bpp` in both cases and copies it to `+0x110`.

The driver also accepts `VirtualScreenWidth`, `VirtualScreenHeight`, `VirtualScreenXpos` and `VirtualScreenYpos` when the selected helper reports mode `2`.

## Panel Selection

`mpulcdtype` is `0x100` on this device, and the panel timing that the board runs matches the first entry of the RGB table in `mpulcd.dll`, which the firmware names `auo`. EBOOT programs that entry field for field. See [docs/soc/lcd.md](../soc/lcd.md) for the timing itself.

The value name is misleading. A value of `0x100` or above does not select the MPU interface. It selects an entry of the RGB table.

The table in this device's `mpulcd.dll` has seven entries. Two of them are 800x480 over a 16 bit bus, `auo` and `qimei`, and they differ in both blanking and pixel clock. A port that guesses between them by resolution alone can pick the wrong one.

## MMIO Mapping

The low-level LCD helper maps two physical windows:

| Physical base |     Size | Runtime role             |
| ------------: | -------: | ------------------------ |
|  `0x20010000` | `0x1000` | LCD controller registers |
|  `0x08000000` | `0x1000` | SYSCTRL registers        |

The driver keeps the controller mapping in a global, and every low-level register helper uses it. The output-mode path that touches `SYSCTRL+0x58` uses the SYSCTRL mapping.

EBOOT and this driver therefore use the same physical controller block.

## Framebuffer Address Model

The driver keeps two framebuffer address domains apart. It maps `LCDVirtualFrameBase` for CPU access with `VirtualAlloc`, then `VirtualCopy(..., protect = 0x204)`, and clears it before GDI draws into it. `LCDPhysicalFrameBase` goes to the surface and controller setup path, and reaches the register writes from there.

The primary framebuffer helper masks the base before it writes:

```c
LCD(0x14) = plane_base & 0x0FFFFFFF;
```

It programs `LCD+0x18`, `LCD+0xA8` and `LCD+0xAC` from the same plane description, and `LCD+0x20` as well, so WinCE also sets up the OSD layer.

The primary surface is a runtime allocation. One cold boot put it at `0x33ED3C00`. The invariant is that the controller reads from the physical base the driver allocated, while CPU drawing goes through the separately mapped virtual base.

The controller never sees a CPU virtual address. Software has to keep the CPU-visible mapping and the DMA-visible base pointing at the same memory, and the framebuffer cannot be cacheable. [docs/soc/lcd.md](../soc/lcd.md) gives the two mappings that work and what they cost.

A driver that owns the controller must program the base itself. The address that EBOOT or WinCE left in `LCD+0x14` is not a fixed layout.
