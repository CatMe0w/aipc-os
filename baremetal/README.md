# Bare metal

Everything here runs on the AK7802 with no operating system. Only openNBOOT is persistent. The rest comes from SD or over USB, and a power cycle removes it.

| Directory | Contents |
| --- | --- |
| [opennboot](opennboot/) | Replaces the stock nboot in NAND block 0. It boots an ARM payload from SD when a card is present, and otherwise boots stock WinCE from NAND |
| [aipc-boot](aipc-boot/) | The payload that openNBOOT hands off to. A GUI bootloader that boots Linux, the GDB stub, or stock WinCE |
| [gdbstub](gdbstub/) | A debug agent that replaces the bootrom USB boot mode |
| [doom](doom/) | DOOM |
| [probes](probes/) | One-shot experiments |
| [lib](lib/) | Shared drivers |

## Used addresses

DDR is 64 MB at `0x30000000..0x33FFFFFF`.

| Address | Contents | Used by |
| --- | --- | --- |
| `0x30000000` | first stage, at most 63 NAND pages | bootrom |
| `0x30008000` | Linux zImage | `AUTO_ZRELADDR` plus `TEXT_OFFSET` |
| `0x30036000` | SVC stack top | OEM nboot |
| `0x30037FD4` | EBOOT container, entry at `0x30038000` | OEM nboot |
| `0x30110000` | top of the running EBOOT image | OEM EBOOT |
| `0x30110000..0x301FFFFF` | log pool, see below | ours |
| `0x30200000` | WinCE kernel, or DOOM when DOOM runs | OEM EBOOT, ours |
| `0x309E0000` | DOOM stack, top at `0x30A00000` | ours |
| `0x30B00000` | DOOM IWAD, about 4 MB | ours |
| `0x30FFFF00` | IRQ stack top | OEM EBOOT |
| `0x32000000` | probe image, results at `0x32008000` | ours |
| `0x33000000` | openNBOOT SD payload, at most 10 MB | ours |
| `0x33800000` | payload stack top | ours |
| `0x33A00000` | `gdbstub.bin`, the build that aipc-boot loads | ours |
| `0x33B00000` | framebuffer, 800x480 RGB565, LCD DMA base `0x03B00000` | OEM EBOOT |
| `0x33ED3C00` | WinCE runtime framebuffer | OEM WinCE |

## Log pool

`0x30110000..0x301FFFFF` holds fifteen 64 KB windows. They go out from the top down:

| Window       | Writer                      |
| ------------ | --------------------------- |
| `0x301F0000` | openNBOOT                   |
| `0x301E0000` | bootbin (openNBOOT test)    |
| `0x301D0000` | gdbstub trace, previous run |
| `0x301C0000` | gdbstub trace, current run  |
| `0x301B0000` | aipc-boot                   |
| `0x301A0000` | DOOM                        |

The pool sits in the gap between the top of the running EBOOT image and the WinCE kernel load address. A log therefore survives a WinCE handoff, and it is still readable after the device boots WinCE. No address survives everything. A Linux zImage overwrites this range, and WinCE takes everything above `0x33B00000` once it runs. When a kernel load destroys a log, the UART still has it, because every writer here mirrors to the UART.

That overlap works in both directions, and the second direction is the dangerous one. The lines logged after a load corrupt any payload that covers the pool. One `log_puts()` call writes into the middle of the image, and a compressed kernel then fails to decompress with no output at all. Anything that loads past `0x30110000` must call `log_detach()` first. That call gives up the DDR half of the log and keeps the UART. The EBOOT container ends at `0x3009BFD4` and never reaches the pool, which is why the WinCE path keeps its log.

`opennboot log` reads the whole pool from the host. It takes a `--slot` name from the table above, or `--all`.
