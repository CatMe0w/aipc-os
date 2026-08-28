# SYSCTRL timers

The AK7802 has five timers inside the system controller block. Each one is a 26 bit down counter with its own control word and its own read only live counter.

## Registers

| Timer | Control | Live counter |
| --- | --- | --- |
| 1 | `SYSCTRL+0x18` | `SYSCTRL+0x100` |
| 2 | `SYSCTRL+0x1c` | `SYSCTRL+0x104` |
| 3 | `SYSCTRL+0x20` | `SYSCTRL+0x108` |
| 4 | `SYSCTRL+0x24` | `SYSCTRL+0x10c` |
| 5 | `SYSCTRL+0x28` | `SYSCTRL+0x110` |

`SYSCTRL` is at physical `0x08000000`.

`SYSCTRL+0x2c` and `SYSCTRL+0x114` are not a sixth timer. They hold unrelated data. Do not map them.

### Control word

| Bits | Name | Access | Behaviour |
| --- | --- | --- | --- |
| 25:0 | count | read write | The reload value. A read gives this value back, never the live count. |
| 26 | EN | read write | The counter runs while this bit is one. |
| 27 | LOAD | write only | A strobe. It copies bits 25:0 into the live counter. It always reads back as zero. |
| 28 | CLEAR | write only | A strobe. It clears the interrupt status. It always reads back as zero. |
| 29 | STA | read only | The interrupt status. A write does not set it. |
| 31:30 | - | - | Not implemented. |

All 26 count bits hold, thus the longest period is 5.59 s.

### Live counter

Read only. It holds the current value of the down counter. This is the only register that gives a live count. The low bits of the control word give the reload value back, which is a common source of error when reading the AK98 source.

## Clock

All five timers take the 12 MHz crystal directly. The rate does not follow the PLL, and cpufreq or a DDR frequency change does not affect it. There is no clock gate for the timers.

A measurement against a host clock over 10.8 s gave 11.9915 MHz, which is 0.071 percent below 12 MHz and well inside the error of the method.

## Interrupts

Each timer raises its own second level interrupt in the system controller. The second level register is `SYSCTRL+0x4c`, with the enables in bits 10:0 and the raw status in bits 26:16.

| Timer | Enable bit | Status bit |
| --- | --- | --- |
| 1 | 5 | 21 |
| 2 | 4 | 20 |
| 3 | 3 | 19 |
| 4 | 2 | 18 |
| 5 | 1 | 17 |

Bit 24 of the same register is the RTC ready status. It is not a timer.

## The counter reloads by itself

The counter does not stop at zero. It sets `STA` and reloads the count field in the same step, then counts down again. `STA` is sticky until software writes `CLEAR`, and a set `STA` does not stop the counter.

The reload value is the count field of the control word. There is no separate latch behind `LOAD`. A control write that carries a zero count field therefore stops the timer at the end of the period that is already in flight.

A full scale counter is a clean free running counter. Four wraps of a counter loaded with `0x3ffffff` each measured exactly 2^26 ticks, with no drift and no stall at zero.

Three results follow.

- A timer loaded with `0x3ffffff` is a free running 26 bit down counter that needs no interrupt. Read it once, invert it, and mask it to 26 bits to get an up counter.
- A periodic timer needs no reload in its interrupt handler. One write of `period | CLEAR | EN` acknowledges the interrupt and lets the same period run on, with no drift, because the hardware has already reloaded.
- Every control write to a running timer must carry the period. A write without it sets the next reload to zero.

## A free running timer keeps its status bit set

A timer with a full scale count reaches zero 5.59 s after it starts. It sets `STA`, reloads, and counts down again. `STA` is sticky, thus the bit stays set from that moment until software writes `CLEAR`.

The count stays correct. A set `STA` does not stop the counter and does not change its period.

Two results follow.

- A free running counter needs no attention while its enable bit in `SYSCTRL+0x4c` is clear. The second level controller gates each status bit with its enable bit, thus a masked timer raises no interrupt.
- Do not enable the second level interrupt of a free running timer. Its status bit is already set, thus the interrupt asserts at once. A handler that does not write `CLEAR` cannot make it stop, and the system controller interrupt asserts again immediately.

To use the interrupt of a free running timer, acknowledge it with a write of `0x3ffffff | CLEAR | EN`, which carries the count field.

## Changing the period

A write to the count field alone does not change a period that is already in flight. The live counter keeps the earlier value and counts on. The new value takes effect at the next automatic reload. `LOAD` copies the count field into the live counter at once. The first read after `LOAD` is about 10 ticks below the loaded value, which is the cost of the bus access. There is no other start up delay.

`LOAD` needs its own write. A write that puts a new value in the count field and strobes `LOAD` in the same access raises the interrupt status at once. The count itself loads correctly, only the interrupt is wrong.

Over 1000 attempts of each form:

| Form | Interrupt raised at once |
| --- | --- |
| One write of `count \| EN \| LOAD` | 1000 of 1000 |
| `count`, then `count \| EN \| LOAD` | 0 of 1000 |

The second write of the second form also carries `LOAD`, but its count field does not change. The trigger is a change of the count field in the same access as the strobe, not the strobe alone.

Always give the count its own write:

```c
writel_relaxed(count, reg);
writel_relaxed(count | CTRL_EN | CTRL_LOAD, reg);
```

A periodic timer hides this fault, because it loads once and then acknowledges with no `LOAD`. A one shot timer loads a new count for every event, thus every event interrupts at once and the machine drowns in interrupts.

## Reading the counter is expensive

A read of a system controller register costs about 580 ns with the caches off, against 65 ns for an empty loop. Two reads cost about twice that. A clocksource read is on a hot path, thus read the live counter once and let the kernel handle the wrap with a 26 bit mask, instead of counting overflows in software.

These figures come from a bare metal run with the caches and MMU off. A DRAM read cost the same as a system controller read in that run, which shows the caches were off. Treat the numbers as a worst case and the difference between one read and two as the reliable part.

## Timers in use by other firmware

The original WinCE firmware uses timer 4 for its system tick. EBOOT uses timer 5 for software timekeeping. Timers 1, 2 and 3 are free (our Linux port takes 1 and 2).

The WinCE firmware sets its own longest period to 2796 ms, which is half of the full range. Measurement found no hardware reason for that limit. Treat it as a margin that the vendor chose.
