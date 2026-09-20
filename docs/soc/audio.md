# Audio DAC

The AK7802 carries a stereo DAC on the die, with an analog output stage and a headphone output. No codec part sits on the board. The digital side of the block holds two registers. Everything else that the DAC needs is in the system controller: the clock, the analog power, the input select of the headphone output.

The register names below follow the public header `anyka_cpu_780x.h` in Intrisit8000. That repository holds two revisions of that file, and the later one, under `Include/platform/AK8802/`, is the one that describes this chip. It puts the two analog control registers at `SYSCTRL+0x5c` and `SYSCTRL+0x64`, it gives the DAC block an address, and it marks four registers as 7802 specific. The earlier one, under `Include/platform/AK7802/`, puts the analog control registers at `+0x20` and `+0x30`, gives `+0x30` to the PWM as well, and names no DAC block. The device agrees with the later revision. Where this document differs from both, a measurement is the reason.

## Registers

The DAC block is at physical `0x2002E000`.

| Offset | Name |
| --- | --- |
| `+0x00` | DAC configuration |
| `+0x04` | I2S configuration |
| `+0x08` | CPU data port |

### DAC configuration

| Bit | Behaviour |
| --- | --- |
| 0 | The DAC controller runs while this bit is one. |
| 1 | One takes data from an L2 buffer, zero from the CPU data port. |
| 2 | Mute. The DAC repeats the last sample instead of taking new data. |
| 3 | Interrupt the processor for data. Leave it clear on the L2 path. |
| 4 | Not implemented. A write does not hold. |

The AK7802 has one sample format and no packed mode, which [Sample format](#sample-format) describes. Bit 4 selects a packed format on the AK98, where one 32 bit word carries two 16 bit samples, and on the AK7802 it reads back as zero whatever the write.

### I2S configuration

| Bits | Behaviour |
| --- | --- |
| 4:0 | The word length, in bits. |
| 5 | Channel polarity. |

The word length field holds the bit count itself. A write of 16 gives correct output. The AK98 source writes 15 for the same intent, thus do not carry that value over.

### System controller

These are the system controller registers the DAC needs. `SYSCTRL` is at physical `0x08000000`.

| Register | Bits | Behaviour |
| --- | --- | --- |
| `SYSCTRL+0x04` | 5:0, 20:17 | The PLL1 multiplier and divider. [Clock](#clock) has the formula. |
| `SYSCTRL+0x08` | 20:13 | The DAC clock divider, less one. |
| `SYSCTRL+0x08` | 21 | The DAC clock runs while this bit is one. |
| `SYSCTRL+0x08` | 24 | Zero holds the DAC in reset. |
| `SYSCTRL+0x08` | 26 | One gates the clock away from the DAC. |
| `SYSCTRL+0x08` | 30 | The side that feeds the DAC. One is the CPU side, zero is the audio processor. |
| `SYSCTRL+0x0c` | 1 | Zero runs the clock of the DAC controller. |
| `SYSCTRL+0x0c` | 3 | Zero runs the clock of the L2 controller. |
| `SYSCTRL+0x0c` | 17 | One holds the DAC controller in soft reset. |
| `SYSCTRL+0x58` | 23 | One connects the internal DAC and ADC. Zero sends the stream to the external I2S pins. |
| `SYSCTRL+0x5c` | 0 | Zero powers the reference voltage. |
| `SYSCTRL+0x5c` | 15, 14, 13 | Zero powers the three analog stages of the DAC output. |
| `SYSCTRL+0x5c` | 18:16 | The input of the headphone output. 1 is the DAC, 2 is line in, 4 is the microphone, and the three bits combine. Zero mutes it. |
| `SYSCTRL+0x5c` | 19 | Zero powers the headphone output. |
| `SYSCTRL+0x5c` | 20 | One holds the analog output still across a power step. |
| `SYSCTRL+0x5c` | 24 | Zero powers the common mode voltage. |
| `SYSCTRL+0x64` | 13 | One enables the DAC. |
| `SYSCTRL+0x64` | 16:14 | The oversample ratio index. [Clock](#clock) has the table. |

`SYSCTRL+0x58` also holds the USB enable field, thus write it read-modify-write. The gain fields of `SYSCTRL+0x5c` at bits 7:6 and 9:8 belong to the line and microphone inputs. Nothing in the path from the DAC to the headphone output has a gain field.

## Clock

The sample rate comes from PLL1 through two dividers:

```
PLL1        = 4 MHz x (62 + SYSCTRL+0x04[5:0]) / (1 + SYSCTRL+0x04[20:17])
DAC clock   = PLL1 / (SYSCTRL+0x08[20:13] + 1)
sample rate = DAC clock / OSR
```

PLL1 is 248 MHz on this device. The divider takes 1 to 256, and the field holds the value less one. The oversample ratio comes from a table of eight, and `SYSCTRL+0x64[16:14]` is the index:

| Index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| OSR | 256 | 272 | 264 | 248 | 240 | 136 | 128 | 120 |

Both dividers are coarse, thus a wanted rate needs a search across the eight ratios for the pair that lands closest. 44100 Hz reaches its closest point at divider 22 and OSR 256, which is 44034 Hz.

A measurement confirms the formula. Playing a known number of frames and timing the blocking writes gives the rate the hardware really ran at. The first two columns come from the formula, and the third from that measurement:

| Requested | Reached at 248 MHz | Measured | Reached at 320 MHz |
| --- | --- | --- | --- |
| 8000 | 8000.0 | 8028.1 | 8003.2 |
| 22050 | 22017.0 | 22037.5 | 22038.6 |
| 32000 | 31991.7 | 32013.5 | 32051.3 |
| 44100 | 44034.1 | 44052.8 | 43859.6 |
| 48000 | 47987.6 | 48003.2 | 48019.2 |

The 44100 row is the tight one: 44052 Hz measured against 44034 Hz predicted is 0.04 percent apart. The 8000 row carries more error from the method than from the clock, because the buffer of that run held 1.02 s of a 2 s measurement.

The fourth column is the formula at a PLL1 of 320 MHz, which is 4 MHz x 80 and gives an ASIC clock of 160 MHz. Nothing on the device ran at that setting, thus that column is arithmetic and not measurement. The grid moves with PLL1, and it does not move uniformly: 48000 stays inside 0.05 percent at both settings, while 44100 goes from 0.15 percent low to 0.55 percent low. All eight ratios against all 256 dividers hold nothing closer to 44100 at 320 MHz.

PLL1 is also the clock the processor runs from, thus a change of the processor frequency changes the sample rate, and a change while the DAC runs shifts the rate under the stream.

## Sample format

The DAC takes one 32 bit word per sample and reads the sample from the high half. The low half is ignored. A frame is two words, left first.

This is the only format the DAC has.

## Data path

Playback data reaches the DAC through an L2 buffer, the same shared SRAM the MMC and UART paths use. The DAC is device 9 in the L2 device list, thus `L2CTRL+0x90` bits 29:27 hold the buffer number that serves it. The eight common buffers hold 512 bytes each.

Two selects have to agree for this path: `SYSCTRL+0x08` bit 30 at one, and DAC configuration bit 1 at one.

The DMA count is in units of 64 bytes, and a transfer larger than one buffer is normal: the L2 controller refills the buffer as the DAC drains it. A transfer therefore finishes at the rate the DAC consumes, not at bus speed, and its completion is what paces a period.

## Start order

The analog stages need their steps in order. This order runs the part:

1. Program the divider and the oversample ratio. Both take effect across a DAC reset.
2. Run the clocks of the DAC controller and the L2 controller, run the DAC clock, and select the CPU side.
3. Soft reset the DAC controller, then reset the DAC.
4. Set the word length. Set mute.
5. Connect the internal DAC.
6. Ungate the DAC clock.
7. Enable the DAC in `SYSCTRL+0x64`.
8. Select the L2 source, then run the DAC controller. One bit per write, source before enable.
9. Power the reference and the common mode voltage, hold the analog output still so that the step does not reach the speakers as a click, wait 10 ms, then power the three output stages.
10. Power the headphone output and select the DAC as its input.
11. Start the first transfer, then clear mute and release the output.

Power down reverses it.

A wrong side select in step 2 raises no error. With it at the audio processor the DAC never takes anything out of the L2 buffer, the transfer never finishes, and the buffer content repeats as noise.

## On this board

Two LM4890 mono amplifiers drive the speakers, one per channel, from the headphone output. Their gain is fixed by resistors at 26 dB. Neither they nor the DAC has a gain control, thus a volume control has to scale the samples.

One line enables both amplifiers, on GPIO69. The line is active high, although the net name is `AMPEN_N`. Two things say so. The jack detect pulls the same line low through a diode, which mutes the speakers when a headphone goes in, and a measurement gives sound when the line is high and silence when it is low.

The panel group must not take GPIO69. `SYSCTRL+0x78` bit 27 hands GPIO69 and GPIO70 to the panel as `LCD_Data16` and `LCD_Data17`, and a panel of sixteen data lines uses neither. With the bit set, the pixel stream drives the amplifier enable, and the speakers carry the switching as noise. See [gpio-crosswalk.md](../bootrom/gpio-crosswalk.md).

The headphone jack takes the headphone output through 10 ohm in each channel, referenced to the VMID pin. Jack detect reaches GPIO1, which is one of the JTAG pads.

The microphone jack is wired. The board microphone is not fitted, and neither line input nor line output reaches a connector.

## Unresolved

- Screen content couples into the speakers as audible noise while the amplifiers are on. The pad conflict above is not the path, because the noise stays after GPIO69 leaves the panel group. The remaining candidates are the power supply and the analog section of the die.
- The capture side is not exercised. ADC2 and ADC3 have a configuration register at `0x2002D000`, and `SYSCTRL+0x5c` bits 5:3 select their input.
