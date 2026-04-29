# Display Driver

The WinCE display driver in `NK.ecec_01` is `anykaDisp.dll`. It owns the LCD controller after NK starts and replaces the framebuffer state left behind by EBOOT. This document records the parts of the driver that are now understood well enough to guide a native LCD driver.

`mpulcd.dll` is still useful for panel-side command details, but it is not the component that maps the LCD controller or programs the framebuffer base. The controller programming path described here is in `anykaDisp.dll`.

## Registry Configuration

During display context initialization, `anykaDisp.dll` opens its display registry key under `HKLM\Drivers\Display\...` and reads the configuration values used to build the runtime display state. The important fields are framebuffer addresses, size, pixel format, refresh intervals, and the MPU LCD type selector.

| Registry value | Display context offset | Meaning |
| --- | ---: | --- |
| `LCDPhysicalFrameBase` | `+0x44` | Address written into the LCD controller framebuffer path |
| `LCDVirtualFrameBase` | `+0xD4` | Address used for the CPU/GDI framebuffer mapping |
| `FrameBufferSize` | `+0x104` | Total framebuffer allocation size |
| `bpp` | `+0x7C` | Pixel depth; also copied to `+0x110` |
| `refreshintervalstatic` | `+0xF8` | Static refresh interval |
| `refreshintervalvideo` | `+0xFC` | Video refresh interval |
| `width` | `+0x08` | Active width when the MPU panel helper is not used |
| `height` | `+0x0C` | Active height when the MPU panel helper is not used |
| `mpulcdtype` | `+0xF0` | Panel/helper selector |
| `HWCursor` | `+0x48` | Hardware cursor enable, only used on the MPU LCD path |

When `mpulcdtype >= 0x100`, the driver creates an MPU LCD helper object and derives the active size from that object. It also accepts `VirtualScreenWidth`, `VirtualScreenHeight`, `VirtualScreenXpos`, and `VirtualScreenYpos` when the selected helper reports mode `2`. In the non-MPU path, `width`, `height`, and `bpp` are taken directly from the registry.

## MMIO Mapping

The low-level LCD helper maps two physical windows:

| Physical base | Size | Runtime role |
| ---: | --: | --- |
| `0x20010000` | `0x1000` | LCD controller registers |
| `0x08000000` | `0x1000` | SYSCTRL registers |

The LCD controller mapping is stored globally and used by all of the low-level register helpers. The SYSCTRL mapping is used by the output-mode path that touches `SYSCTRL+0x58`.

This confirms that the LCD controller base used by EBOOT and by the WinCE display driver is the same physical block, `0x20010000`.

## Framebuffer Address Model

The driver keeps two framebuffer address domains separate. `LCDVirtualFrameBase` is mapped for CPU access with `VirtualAlloc` followed by `VirtualCopy(..., protect = 0x204)`, then cleared by the driver before GDI starts using it. `LCDPhysicalFrameBase` is passed to the surface/controller setup path and eventually reaches the LCD register writes.

The primary framebuffer programming helper writes the base address to `LCD+0x14` after masking off the high nibble:

```c
LCD(0x14) = plane_base & 0x0FFFFFFF;
```

It also programs `LCD+0x18`, `LCD+0xA8`, `LCD+0xAC`, and `LCD+0x20` from the same plane description. This is enough to establish that the LCD controller does not consume the CPU virtual framebuffer address directly. Software must keep the CPU-visible mapping and the LCD DMA-visible base coherent.

Cold-boot memory analysis previously observed a WinCE primary display surface at `0x33ED3C00` on one run. That address should be treated as a runtime allocation result, not a fixed hardware constant. The invariant is that WinCE programs `LCD+0x14` from its allocated physical framebuffer base, while CPU-side drawing goes through the separately mapped virtual base.

## Confirmed Register Fields

The WinCE driver confirms several fields that EBOOT alone left ambiguous.

| Register | Meaning now confirmed |
| --- | --- |
| `LCD+0x14` | Framebuffer/plane base, low 28 bits |
| `LCD+0x18` | Active size word, `(width << 16) | height` |
| `LCD+0xAC` | Packed active size, `(width << 10) | height` |
| `LCD+0xB0` | Packed active size, `(width << 10) | height` |
| `LCD+0xE8` | Pixel clock divider, `2 * (div & 0x7F) | 0x101` |

For the built-in 800x480 panel, `LCD+0x18 = 0x032001E0` and `LCD+0xAC = LCD+0xB0 = 0x000C81E0`. These values encode 800 and 480 directly:

```c
0x032001E0 = (800 << 16) | 480;
0x000C81E0 = (800 << 10) | 480;
```

The panel timing path writes `LCD+0x40..0x58`, then updates the control register. It constructs the control word around `0x80A80000` or `0x80A88000`, depending on a mode bit in the panel descriptor. The exact bit-level meaning of the high control bits is still not fully decoded.

## MMU and Cache Implications

The LCD controller reads from the address programmed into `LCD+0x14`. Enabling the CPU MMU does not change that address. If a native port enables the MMU or D-cache, it must still ensure that CPU stores target the memory backing the LCD base register, and that cached framebuffer writes are visible to the LCD DMA engine.

A valid native bring-up must therefore treat the framebuffer specially. An identity-mapped framebuffer is not sufficient if it is cacheable and the cache is not cleaned before scanout. Conversely, writing a WinCE-observed virtual or physical framebuffer address only works when the LCD controller has been programmed to read from the corresponding base.

## Unresolved

- The exact registry subkey names for the primary and secondary display instances need to be named from registry data rather than inferred from the embedded strings alone.
- The bit-level meaning of `LCD+0x00`, `LCD+0x10`, and `LCD+0x40..0x58` is still only partially decoded.
- The MPU LCD helper's panel command tables should be documented separately from the controller register path.
- A native driver still needs a confirmed cache maintenance rule for framebuffer scanout when D-cache is enabled.
