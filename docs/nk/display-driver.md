# Display Driver

The WinCE display driver in `NK.ecec_01` is `anykaDisp.dll`. It owns the LCD controller after NK starts, and it replaces the framebuffer state that EBOOT left behind. This document records the parts of the driver that we understand well enough to guide a native LCD driver.

`mpulcd.dll` is still useful for panel-side command details, but it is not the component that maps the LCD controller or programs the framebuffer base. The controller programming path here is in `anykaDisp.dll`.

## Registry Configuration

During the display context initialization, `anykaDisp.dll` opens its display registry key under `HKLM\Drivers\Display\...`. It reads the configuration values that build the runtime display state. The important fields are the framebuffer addresses, the size, the pixel format, the refresh intervals, and the MPU LCD type selector.

| Registry value | Display context offset | Meaning |
| --- | --: | --- |
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

With `mpulcdtype >= 0x100`, the driver creates an MPU LCD helper object and takes the active size from that object. It also accepts `VirtualScreenWidth`, `VirtualScreenHeight`, `VirtualScreenXpos` and `VirtualScreenYpos` when the selected helper reports mode `2`. On the non-MPU path, `width`, `height` and `bpp` come straight from the registry.

## MMIO Mapping

The low-level LCD helper maps two physical windows:

| Physical base |     Size | Runtime role             |
| ------------: | -------: | ------------------------ |
|  `0x20010000` | `0x1000` | LCD controller registers |
|  `0x08000000` | `0x1000` | SYSCTRL registers        |

The driver keeps the LCD controller mapping in a global, and every low-level register helper uses it. The output-mode path that touches `SYSCTRL+0x58` uses the SYSCTRL mapping.

This confirms that EBOOT and the WinCE display driver use the same physical LCD controller block, `0x20010000`.

## Framebuffer Address Model

The driver keeps two framebuffer address domains apart. It maps `LCDVirtualFrameBase` for CPU access with `VirtualAlloc`, then `VirtualCopy(..., protect = 0x204)`, and clears it before GDI starts to use it. `LCDPhysicalFrameBase` goes to the surface and controller setup path, and it reaches the LCD register writes from there.

The primary framebuffer programming helper writes the base address to `LCD+0x14` after it masks off the high nibble:

```c
LCD(0x14) = plane_base & 0x0FFFFFFF;
```

It also programs `LCD+0x18`, `LCD+0xA8`, `LCD+0xAC` and `LCD+0x20` from the same plane description. This is enough to establish that the LCD controller does not consume the CPU virtual framebuffer address directly. Software must keep the CPU-visible mapping and the LCD DMA-visible base coherent.

An earlier cold-boot memory analysis found a WinCE primary display surface at `0x33ED3C00` on one run. Treat that address as the result of a runtime allocation, not as a fixed hardware constant. The invariant is that WinCE programs `LCD+0x14` from the physical framebuffer base that it allocated, while CPU-side drawing goes through the separately mapped virtual base.

## Confirmed Register Fields

The WinCE driver confirms several fields that EBOOT alone left ambiguous.

| Register   | Meaning now confirmed                  |
| ---------- | -------------------------------------- |
| `LCD+0x14` | Framebuffer/plane base, low 28 bits    |
| `LCD+0x18` | Active size word, `(width << 16)       | height` |
| `LCD+0xAC` | Packed active size, `(width << 10)     | height` |
| `LCD+0xB0` | Packed active size, `(width << 10)     | height` |
| `LCD+0xE8` | Pixel clock divider, `2 * (div & 0x7F) | 0x101`  |

For the built-in 800x480 panel, `LCD+0x18 = 0x032001E0` and `LCD+0xAC = LCD+0xB0 = 0x000C81E0`. These values encode 800 and 480 directly:

```c
0x032001E0 = (800 << 16) | 480;
0x000C81E0 = (800 << 10) | 480;
```

The panel timing path writes `LCD+0x40..0x58`, then updates the control register. It builds the control word around `0x80A80000` or `0x80A88000`, which depends on a mode bit in the panel descriptor. The bit-level meaning of the high control bits is still not fully decoded.

## MMU and Cache Implications

The LCD controller reads from the address in `LCD+0x14`. The CPU MMU does not change that address. A native port that enables the MMU or the D-cache must still make sure that CPU stores reach the memory behind the LCD base register, and that cached framebuffer writes are visible to the LCD DMA engine.

A valid native bring-up must therefore treat the framebuffer specially. An identity-mapped framebuffer is not sufficient when it is cacheable and nothing cleans the cache before scanout. A WinCE-observed virtual or physical framebuffer address works only when the LCD controller reads from the matching base.

## Unresolved

- The exact registry subkey names for the primary and the secondary display instances. They need registry data, not an inference from the embedded strings alone.
- The bit-level meaning of `LCD+0x00`, `LCD+0x10` and `LCD+0x40..0x58` is still only partly decoded.
- The panel command tables of the MPU LCD helper need their own document, apart from the controller register path.
- A native driver still needs a confirmed cache maintenance rule for framebuffer scanout with the D-cache enabled.
