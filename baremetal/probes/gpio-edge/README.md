# GPIO interrupt probe

The AK7802 cannot give the power key one hardware interrupt that covers both the press and the release. The general GPIO interrupt is a level interrupt with a selectable polarity, and it serves one polarity at a time. WGPIO is a wake facility, and GPIO3 is not one of its inputs. This probe measured both paths.

The power key is an active low input on GPIO3.

GPIO13 is the control. It is the active low SD card detect input, it maps to WGPIO bit 6, and a card that you pull out is a transition that you know happened. GPIO13 carries the WGPIO result of this probe.

The probe polls, and CPU IRQ and FIQ stay masked. Every result below is therefore about a status bit that did or did not latch, not about an interrupt that reached the core.

The probe does not enter standby. Every result below applies only while the processor runs.

## Registers

The general GPIO interrupt uses an enable bit at `SYSCTRL+0xe0` and a polarity bit at `SYSCTRL+0xf0`. GPIO3 is bit 3 in the input word, the enable word and the polarity word. A set polarity bit selects the low level. There is no edge select, no pending register and no pending clear.

| Offset | Use |
| --- | --- |
| `SYSCTRL+0x3c` | WGPIO trigger polarity |
| `SYSCTRL+0x40` | WGPIO status clear |
| `SYSCTRL+0x44` | WGPIO enable |
| `SYSCTRL+0x48` | WGPIO status |

A set WGPIO polarity bit selects the falling edge.

## Build

```sh
make -C baremetal/probes/gpio-edge/stub
```

The image runs at `0x32000000`, writes a 364 byte result to `0x32008000`, restores every register it changed, and stops on an undefined instruction. The GDB stub then takes over again.

## Run

Start with the SD card fully inserted and the power key released. Connect the device to DC power, because the power key does not respond on USB power alone.

```sh
arm-none-eabi-gdb -ex 'target remote /dev/cu.usbmodem00011'
```

```gdb
restore baremetal/probes/gpio-edge/stub/gpio-edge.bin binary 0x32000000
set $pc = 0x32000000
continue
dump binary memory /tmp/gpio-edge-result.bin 0x32008000 0x3200816c
```

While the target runs, do these in order:

1. Remove the SD card completely.
2. Insert the SD card completely.
3. Press and release the power key.

Every wait has a timeout. Decode the result after the GDB stub takes over again:

```sh
uv run baremetal/probes/gpio-edge/decode.py /tmp/gpio-edge-result.bin
```

## Result

Measured on the v1.58.2 device. The probe reported status complete.

### The general GPIO path reports a level and does not latch an edge

In the table below, armed for the press means the general GPIO polarity selects the low level and the WGPIO polarity selects the falling edge. Armed for the release is the opposite of both.

| Stage | GPIO3 | `INT_STATUS` | GPIO enable/polarity | WGPIO enable/polarity/status |
| --- | ---: | ---: | ---: | ---: |
| initial | 1 | `0x02001000` | `0x00000000`/`0x00000000` | `0x00000000`/`0x00000000`/`0x00000000` |
| armed for the press | 1 | `0x02001000` | `0x00000008`/`0x00000008` | `0xffffffff`/`0xffffffff`/`0x00000000` |
| key down | 0 | `0x0a001000` | `0x00000008`/`0x00000008` | `0xffffffff`/`0xffffffff`/`0x00000000` |
| armed for the release | 0 | `0x0a001000` | `0x00000008`/`0x00000008` | `0xffffffff`/`0x00000000`/`0x00000000` |
| key up | 1 | `0x02001000` | `0x00000008`/`0x00000008` | `0xffffffff`/`0x00000000`/`0x00000000` |

The probe samples each transition twice, immediately and again about 1 ms later. Both samples are identical at every stage, thus nothing appears late.

The difference between the two `INT_STATUS` values is bit 27, the SYSCTRL parent. That bit followed the pin. It appeared when GPIO3 went low, and it cleared on release, with the enable bit and the polarity bit unchanged the whole time. A latched edge would have stayed set until software cleared it.

The GPIO polarity column does not change after the press is armed. The probe never flipped that bit, thus it did not measure what a flip does.

### WGPIO is a wake facility and latches nothing while the processor runs

GPIO3 has no WGPIO bit. The probe enabled all 32 bits at both polarities around the press and the release, and no status bit set. That is the result an unmapped pin gives, thus it says nothing about the WGPIO block. This is why the probe carries a control.

GPIO13 carries the result. The probe enabled WGPIO bit 6 alone, at both polarities:

| Transition | GPIO13 input | WGPIO status | `INT_STATUS` |
| --- | ---: | ---: | ---: |
| card out, rising | `0x00002000` | `0x00000000` | `0x02001000` |
| card in, falling | `0x00000000` | `0x00000000` | `0x02001000` |

The input values show that both physical transitions reached the pin. Bit 6 stayed clear at both polarities. WGPIO therefore does not latch while the processor runs. Its status word reports which pin woke the part, and not what happened during normal operation.

## What a Driver Needs

Mask the line when you handle it. The level path has no pending register, thus the status follows the pin for as long as the pin holds that level. A handler that leaves the line unmasked runs again immediately.

A driver that wants both edges must flip the polarity. The level path raises an interrupt for one polarity at a time, thus a driver that wants the press and the release must flip the polarity bit inside its own handler. That driver owns the debounce and the race inside the flip window. This probe did not measure a flip.
