# Touchpad probes

These probes validate the PS/2 touchpad without a desktop or a Linux input stack. The device schematic connects `TOUCHPAD_CLK` to `GPIO14` and `TOUCHPAD_DAT` to `GPIO15` through the UART1 pin group.

## Build

```sh
make -C baremetal/probes/touchpad/stub
```

The build makes nine images.

| Image | Purpose |
| --- | --- |
| `touchpad-passive.bin` | Observe the two input levels for four seconds. Change no mux, direction, output latch, or pull configuration. |
| `touchpad-enable.bin` | Select GPIO mode, send `F4`, collect replies and motion packets for four seconds, send `F5`, and restore the saved pin state. |
| `touchpad-identify.bin` | Send `F5`, query the device ID with `F2`, and query the status with `E9`. |
| `touchpad-poll.bin` | Enter remote mode, read 40 packets with `EB`, then restore stream mode and disable reporting. |
| `touchpad-init.bin` | Run the complete reset and initialization sequence, collect motion packets for four seconds, and disable reporting. |
| `touchpad-uart-query.bin` | Compare eight UART status replies against a GPIO baseline. |
| `touchpad-uart-motion.bin` | Initialize through GPIO and receive motion through UART for four seconds. |
| `touchpad-uart-irq.bin` | Compare eight UART replies and read the main interrupt status before and after each receive acknowledgment. |
| `touchpad-gpio4.bin` | Sample GPIO4 for ten seconds with the pad pull-down disabled. Does not touch the touchpad. |

Every image runs at `0x32000000` and stops on an undefined instruction, thus gdbstub gets control again. The GPIO images write their result to `0x32008000`. The UART images write theirs to `0x32010000`.

## What the images touch

Every image saves the pin mux, direction, output latch, and pull configuration at entry, then restores it before it stops. The UART images also save and restore the UART control, the threshold configuration, and the L2 path. No image drives either PS/2 line high. An image pulls a line low through GPIO output mode and releases the line through GPIO input mode.

The GPIO4 image also disables the pad pull-down at `SYSCTRL+0x9c` bit 4, then restores it.

The UART images do not restore the UART receive data or the receive index. Run them while the UART is idle and no other software owns its receive buffer.

Every image uses Timer2 for bounded waits and leaves it stopped. Timer2 starts with a full count write, then a second write with `EN` and `LOAD`. See [timer registers](../../../docs/soc/timer.md) for the reason the count needs its own write.

## Run the passive probe

Run the passive probe first. Move one finger on the touchpad while the target continues.

```sh
arm-none-eabi-gdb -ex 'target remote /dev/cu.usbmodem00011'
```

```gdb
restore baremetal/probes/touchpad/stub/touchpad-passive.bin binary 0x32000000
set $pc = 0x32000000
continue
dump binary memory /tmp/touchpad-result.bin 0x32008000 0x3200c8b0
```

```sh
uv run baremetal/probes/touchpad/decode.py /tmp/touchpad-result.bin
```

A passive run with idle-high levels and no transitions is not a touchpad failure. A PS/2 pointing device keeps data reporting disabled until the host sends `F4`.

## Run the active probe

Move one finger during the four-second collection interval.

```gdb
restore baremetal/probes/touchpad/stub/touchpad-enable.bin binary 0x32000000
set $pc = 0x32000000
continue
dump binary memory /tmp/touchpad-result.bin 0x32008000 0x3200c8b0
```

Decode the result with the same command. A complete link validation gives these results:

- The decoder reports `timer running: 1`.
- Both lines are high at idle.
- The `F4` send operation completes.
- The enable reply is a valid `0xFA` frame.
- Finger movement produces valid three-byte packets with changing X or Y deltas.
- The final `F5` operation returns `0xFA`.
- The before and after mux, direction, and output values match.

The result also holds a timestamped transition trace. This trace identifies the send stage that timed out when command transmission fails. It also keeps enough raw line activity to separate a protocol error from an inactive device.

## Identify, poll, and initialize

The remaining GPIO images use the same load address and decoder. Dump their results through `0x3200c8b0`.

Run `touchpad-identify.bin` without user input. It reports the standard PS/2 device ID and the three status bytes that `E9` returns.

Run `touchpad-poll.bin` once while the touchpad is idle and once while moving one finger. It disables automatic reporting, enters remote mode with `F0`, and issues `EB` at 10 Hz. It restores stream mode with `EA` before it returns.

Run `touchpad-init.bin` while moving one finger. The sequence is `FF`, `F3 C8`, `F3 64`, `F3 50`, `F2`, `F3 0A`, `F2`, `E8 03`, `E6`, `F3 14`, and `F4`. The decoder reports the first failed step or prints the motion packets. The image sends `F5` before it restores the saved pin state.

## Run the UART probes

Run the query image without finger movement. It first reads the current status through GPIO. It then sends eight `E9` commands through GPIO and receives each reply through UART at 13600 baud, 8O1.

Run the motion image while moving one finger. It sends the full initialization sequence through GPIO, then receives the `F4` reply and the motion through UART. It returns to GPIO mode for the final `F5`.

```gdb
restore baremetal/probes/touchpad/stub/touchpad-uart-query.bin binary 0x32000000
set $pc = 0x32000000
continue
dump binary memory /tmp/touchpad-uart.bin 0x32010000 0x32015100
```

```sh
uv run baremetal/probes/touchpad/decode_uart.py /tmp/touchpad-uart.bin
```

The decoder returns a nonzero exit code when validation fails.

## Run the GPIO4 probe

`touchpad-gpio4.bin` does not touch the touchpad. Hold the left button down for the whole ten seconds.

```gdb
restore baremetal/probes/touchpad/stub/touchpad-gpio4.bin binary 0x32000000
set $pc = 0x32000000
continue
dump binary memory /tmp/touchpad-gpio4.bin 0x32008000 0x3200a0a8
```

```sh
uv run baremetal/probes/touchpad/decode_gpio4.py /tmp/touchpad-gpio4.bin
```

## Result layout

The GPIO result is 18608 bytes. It holds a 44-word header, 256 timestamped receive frames, and 2048 timestamped line transitions. Timestamps are 12 MHz Timer2 ticks from probe start. The decoder checks the magic, validates the PS/2 start, parity, and stop bits, and prints standard three-byte relative-motion packets.

The UART result is 20736 bytes. It holds a 64-word header and 1024 events. `struct uart_header` names the header fields. Each event holds five words: ticks, status, receive configuration, data, and valid byte count.

The GPIO4 result is 8360 bytes. It holds a 22-word header, ten sample counts, ten high-sample counts, and 1024 timestamped transitions.

The transition trace counts the transitions it drops. **The receive arrays do not count what they drop.** A command-heavy run can fill the transition trace while the decoded receive frames stay valid. A run that fills a receive array cannot show lossless capture.

## Result

The register model and the rules for driver code are in [/docs/nk/touchpad-driver.md](/docs/nk/touchpad-driver.md). This section holds the measurements themselves. Every measurement comes from the v1.58.2 device. The left button of that unit makes contact unreliably. Suspect the switch before the software when a left-button report goes missing.

### The device stays idle until it gets a full initialization sequence

Both lines were idle high. An `F4`-only run received `FA` but no movement packets. `F2` returned device ID `00`, and `E9` returned status `00 02 64`. Remote-mode polling returned valid `08 00 00` packets both at rest and while moving one finger.

The complete initialization sequence received a valid reply at every step. It then captured 76 motion packets during four seconds of finger movement. X and Y held both positive and negative deltas. A later `E9` returned `00 03 14`.

This proves the touchpad, the GPIO14/GPIO15 link, and standard relative PS/2 packets work. It does not establish which subset of the sequence is required.

### The UART receives the same bytes as the GPIO path

The query image sent eight `E9` commands and received eight four-byte replies. Every reply matched the GPIO baseline byte for byte.

The motion image received 244 bytes: one `FA` reply and 81 three-byte packets. X and Y held positive and negative values, and every packet boundary carried the PS/2 synchronization bit. The run reported no receive error, no buffer-full indication, and no record overflow. It handled one-byte and three-byte timeout flushes and two receive-index wraps. The final GPIO `F5` returned `FA`.

### UART reception asserts main interrupt bit 16

The IRQ image enabled the threshold and timeout interrupts. Header words 48, 49, and 50 hold the main interrupt status at `SYSCTRL+0xcc` before reception, during reception, and after acknowledgment. The last two fields accumulate status with bitwise OR.

| Header word | Value |
| --- | --- |
| 48 (before) | `02001000` |
| 49 (during) | `02011000` |
| 50 (after) | `02001000` |

Reception therefore asserts bit 16, and the UART acknowledgment clears it. This image reads the interrupt status through polling. It installs no CPU interrupt handler.

The same run held the clock low after each GPIO command, then selected UART mode. All eight replies still matched the GPIO baseline. A sender can therefore inhibit the reply while it changes the pin mux.

### The left button does not appear on GPIO4

The schematic routes `KEY_L` to GPIO4, thus GPIO4 looks like a second path for the left button. It is not one.

`SYSCTRL+0x9c` bit 4 controls the pad pull-down, and a zero bit enables it. An enabled pull-down holds GPIO4 low whatever the button does, thus a capture in that state carries no information. Two early captures read a constant low for this reason alone.

With the pull-down disabled, the board holds GPIO4 high. Three runs disagree only in what the operator did. One held the left button down for ten seconds. One pressed and released it for four seconds. One left it alone for ten seconds. All three read high in every sample after the settling window below, and none recorded a transition there. The left button therefore does not change GPIO4.

Every run opens with the same settling artifact. The pin reads low for 62 to 68 ms after the probe disables the pull-down. It then chatters across the input threshold for 2 to 7 ms, then stays high. The three runs show this with the button held down, pressed repeatedly, and untouched, thus the pad settles and the button does nothing. Allow the pin at least 100 ms before you read it for any purpose.
