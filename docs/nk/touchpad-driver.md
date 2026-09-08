# PS/2 touchpad

The v1.58.2 device uses a PS/2 touchpad with standard relative motion packets. A stock PS/2 host driver reads it without changes. The touchpad supports one contact. Its WinCE `tpd.dll` reads `COM1:` at 13600 baud with eight data bits, odd parity, and one stop bit. The keyboard uses a separate CH374 USB path.

## Connections

| Signal | Pin | Function |
| --- | --- | --- |
| TOUCHPAD_CLK | GPIO14 | PS/2 clock and UART1 TX pin |
| TOUCHPAD_DAT | GPIO15 | PS/2 data and UART1 RX pin |

`SYSCTRL+0x78` bit 9 selects the GPIO14/GPIO15 pair. Zero selects GPIO. One selects UART1.

The GPIO command sender pulls each line low or releases it as an input. It does not drive a line high.

## Both buttons arrive in the packet

Byte 0 of a motion packet holds the button state.

| Bit | Meaning |
| --- | --- |
| 0 | Left button |
| 1 | Right button |
| 3 | Constant one |

`tpd.dll` drops a packet when bit 3 is clear or bit 2 is set.

The schematic also routes `KEY_L` to GPIO4. That pin does not follow the left button. Do not use it.

GPIO4 shares its pad with the JTAG `RTCK` output, and GPIO[4:0] select JTAG at reset. `SYSCTRL+0x78` bit 1 must be zero before GPIO4 acts as a GPIO. The pad has a pull-down and no pull-up. `SYSCTRL+0x9c` bit 4 keeps that pull-down enabled while the bit is zero, thus GPIO4 reads a constant low in its default state. With the pull-down disabled, the board holds the pin high. The pin stays high while a finger holds the left button down. `tpd.dll` does not read this pin. `KEY_R` does not reach the processor.

## Initialization

The following sequence produced relative motion after a reset:

```
FF
F3 C8 F3 64 F3 50 F2
F3 0A F2
E8 03 E6 F3 14 F4
```

Reset returned `FA AA 00`. Both `F2` commands returned `FA 00`. Every other command byte received `FA`.

This sequence is sufficient. The necessary subset is not known. An `F4` alone receives `FA` but produces no motion. `tpd.dll` does not send this sequence.

The sequence works from a cold start. The device needs no setup from an earlier boot stage.

After this sequence, `F5` followed by `E9` returned `FA 00 03 14`. The status reports stream mode with reporting disabled, resolution 3, and sample rate 20. Before the sequence, the status was `00 02 64`.

## UART reception

UART1 has a register base of `0x20026000`. Its receive buffer is 128 bytes at `0x48001080`.

| Register | Field | Use |
| --- | --- | --- |
| UART+0x00 | Bits 15:0 | Baud divider |
| UART+0x00 | Bit 21 | UART enable |
| UART+0x00 | Bit 22 | Half-step divider adjustment |
| UART+0x00 | Bit 23 | Receive timeout enable |
| UART+0x00 | Bits 26:25 = `10` | Odd parity |
| UART+0x04 | Bit 30 | Receive threshold status, write one to clear |
| UART+0x04 | Bit 28 | Receive threshold interrupt enable |
| UART+0x04 | Bit 22 | Receive timeout interrupt enable |
| UART+0x04 | Bit 2 | Fractional receive timeout, write one to clear |
| UART+0x08 | Bits 17:13 | Next receive word index, modulo 32 |
| UART+0x08 | Bits 24:23 | Valid bytes in the final fractional word |

At the measured ASIC clock of 124 MHz, control value `0x04e0239c` receives 13600-baud touchpad data. See [clock calculation](../eboot/memory-map.md#cpu-clock-formula). Set `L2CTR_DMA_PATH_CFG`, at `0x2002c084`, bits 29:28 as the bootrom does.

The receive index identifies the next word slot. For example, index 1 places the latest word at `0x48001080`. Index 0 places it at `0x480010fc`. Read words from the software index through the slot before the hardware index, with modulo-32 arithmetic.

### Wait for receive status before you read the index

Do not read a new word only because the index changed. The index can change before receive status becomes active. A read at that point splits a three-byte packet into four bytes and puts stale SRAM content into the stream.

Wait for receive status, then read the index and the buffer. When timeout status is set, use the fractional byte count for the last word. Clear the handled status bits after the read. A timeout is a normal packet flush, not a receive failure.

### UART reception raises main interrupt bit 16

Reception asserts bit 16 in the main interrupt status at `SYSCTRL+0xcc`. A write that clears the UART status also clears this main interrupt bit.

### Hold the clock low across the mux change

The sender can hold the clock low after a GPIO command finishes, then select UART mode, then release the clock. The reply still arrives complete. This inhibits the reply while the pin mux changes.

The [touchpad probes](../../baremetal/probes/touchpad/README.md) hold the measurements behind this document, together with the executable experiment and the result decoder.

## Unresolved

Sustained reception under concurrent peripheral load needs validation. No measurement so far can prove lossless reception, because the probe receive arrays do not count what they drop.
