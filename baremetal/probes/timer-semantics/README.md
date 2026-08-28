# SYSCTRL timer semantics probe

This probe finds the behavior of the five SYSCTRL timers.

The timers are at `SYSCTRL+0x18` through `SYSCTRL+0x28`. Each has a read only live counter at `SYSCTRL+0x100` through `SYSCTRL+0x110`.

## Build

```sh
make -C baremetal/probes/timer-semantics/stub
```

The build makes three images.

| Image | Purpose |
| --- | --- |
| `timer-semantics.bin` | The register model, the behavior at zero, all five timers, and the read cost. Runs for about one second. |
| `timer-wrap.bin` | Four wraps of a full scale counter, with an exact measurement of each cycle. Runs for about 23 seconds. |
| `timer-load.bin` | Whether one write can change the count field and strobe LOAD together. Runs in a few milliseconds. |

Each image runs at `0x32000000` and writes its result to `0x32008000`. Each stops on an undefined instruction, thus gdbstub gets control again.

## Run

Change `/dev/cu.usbmodem00011` to your own device node.

```sh
arm-none-eabi-gdb -batch \
  -ex 'target remote /dev/cu.usbmodem00011' \
  -ex 'restore baremetal/probes/timer-semantics/stub/timer-semantics.bin binary 0x32000000' \
  -ex 'set $pc = 0x32000000' \
  -ex 'continue' \
  -ex 'dump binary memory /tmp/timer-result.bin 0x32008000 0x32010000'
```

```sh
uv run baremetal/probes/timer-semantics/decode.py /tmp/timer-result.bin
```

Add `--dump-crossing 64` to print the raw samples around the point where the counter reaches zero.

Run the wrap probe the same way. Allow it 30 seconds. The decoder finds the format from the magic number.

```sh
arm-none-eabi-gdb -batch \
  -ex 'set remotetimeout 120' \
  -ex 'target remote /dev/cu.usbmodem00011' \
  -ex 'restore baremetal/probes/timer-semantics/stub/timer-wrap.bin binary 0x32000000' \
  -ex 'set $pc = 0x32000000' \
  -ex 'continue' \
  -ex 'dump binary memory /tmp/timer-wrap.bin 0x32008000 0x32009000'
```

Run the LOAD probe the same way. It needs only the first 256 bytes of the result.

```sh
arm-none-eabi-gdb -batch \
  -ex 'target remote /dev/cu.usbmodem00011' \
  -ex 'restore baremetal/probes/timer-semantics/stub/timer-load.bin binary 0x32000000' \
  -ex 'set $pc = 0x32000000' \
  -ex 'continue' \
  -ex 'dump binary memory /tmp/timer-load.bin 0x32008000 0x32008100'
```

Measure the clock rate against the host clock with `rate.gdb`. It needs no stub, because it writes the timer register through gdb and reads the counter while the target is stopped.

```sh
arm-none-eabi-gdb -batch -x baremetal/probes/timer-semantics/rate.gdb
```

Each image masks the five timer enable bits in `SYSCTRL+0x4c` for the length of the run, thus no timer can interrupt the CPU. Each restores those five bits at the end and leaves the timers stopped. No image writes a register above `SYSCTRL+0x28`.

## Result

The register model, the interrupt bit map, and the rules these measurements give for driver code are in [/docs/soc/timer.md](/docs/soc/timer.md). This section holds the measurements themselves.

### Register model

Bits 25:0 hold the reload value, and a read of the control register gives that value back, not the live count. Bits 26 to 29 are `EN`, `LOAD`, `CLEAR` and `STA`. `LOAD` and `CLEAR` are write only strobes and always read back as zero. A write cannot set `STA`. Bits 31:30 do not stick.

All 26 count bits hold.

`SYSCTRL+0x2c` and `SYSCTRL+0x114` hold unrelated data. They are not a sixth timer.

### The counter reloads by itself

The counter does not stop at zero. It sets `STA` and reloads the count field in the same step, then it counts down again. The probe watched 770 samples after `STA` came up. The counter kept its period the whole time, thus a set `STA` does not stop it.

### A full scale cycle is exactly 2^26 ticks

The wrap probe loaded `0x3ffffff` and watched four wraps over 22.4 s. It added `(previous - current) & 0x3ffffff` to a 64 bit total at every read, thus the total between two wraps is the cycle length. A detection read never lands on the reload itself, so the decoder subtracts how far past the reload each detection landed.

All three measured cycles came to 67108864 ticks, which is 2^26, with no error at all. The samples on both sides of each wrap fall on a straight line. The counter does not stall and it loses no time.

### The clock is 12 MHz

`rate.gdb` read the counter four times over 10.8 s of host time and got 11.9915 MHz, which is 0.071 percent below 12 MHz. The width of the read brackets alone allows 2 percent, thus the result confirms 12 MHz and rules out the other common crystal rates. Every other time in this report comes from that rate.

### The reload value comes from the count field

The probe acknowledged an expired timer with a write of `CLEAR | EN` and a zero count field. The counter finished the period that was already in flight, reached zero, and stayed at zero. The probe then acknowledged with a write of `period | CLEAR | EN`. The counter reloaded the period and continued. There is no separate latch behind `LOAD`.

A write of `CLEAR` alone stops the timer, because it puts `EN` low and the count field at zero. The probe used this to stop each timer and the live counter froze.

### LOAD is necessary for a new period

The probe wrote a new count with `EN` and no `LOAD`. The live counter kept the earlier value and went on. A write of `LOAD` put the new value in the live counter at once. The first read after `LOAD` was about 10 ticks below the loaded value, which is the cost of the bus access. There was no start up delay beyond that.

[/baremetal/lib/timer.c](/baremetal/lib/timer.c) and the touchpad probes start their timer with no `LOAD`. They work only because the period that was already in the register is large.

### One write cannot change the count and strobe LOAD

The probe compared two forms with a count of `0x100000`, which needs 87 ms to reach zero, and repeated each form 1000 times.

| Form | Control register after the write | Interrupt raised at once |
| --- | --- | --- |
| One write of `count \| EN \| LOAD` | `0x24100000`, thus `STA` is set | 1000 of 1000 |
| `count`, then `count \| EN \| LOAD` | `0x04100000`, thus `STA` is clear | 0 of 1000 |

The live counter read `0xffff1` after both forms, thus both loaded the count correctly. The count itself is right and only the interrupt is wrong.

The second write of the second form also carries `LOAD`, but its count field does not change. The trigger is therefore a change of the count field in the same access as the strobe, not the strobe alone.

### All five timers agree

Timer 1 to timer 5 gave the same result for the same short period. Each has its own control register, its own live counter, and its own status bit in `SYSCTRL+0x4c`.

Bit 24 of `SYSCTRL+0x4c` was set for the whole run. It is the RTC ready status and it is not a timer.

### Read cost

The probe read the same register 4096 times and measured the time with another timer.

| Operation | Cost per pass |
| --- | --- |
| Empty loop | 65 ns |
| One SYSCTRL read | 584 ns |
| Two SYSCTRL reads | 1083 ns |
| One DRAM read | 584 ns |

A DRAM read costs as much as a SYSCTRL read, thus the caches were off for the run. Read these numbers as a worst case. The important number is the difference: a second SYSCTRL read adds about 500 ns.

The probe ran with the caches and MMU disabled.
