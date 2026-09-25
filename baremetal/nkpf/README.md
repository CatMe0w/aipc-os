# nkpf

nkpf is the custom firmware layer for stock WinCE.

Each time aipc-boot starts WinCE, nkpf patches the WinCE kernel (NK) in DDR, before NK runs.

The name stands for *NK patchfinder*, after the kernel patchfinder (KPF) of [checkra1n](https://checkra.in). The pattern finder and the console come from [pongoOS](https://github.com/checkra1n/pongoOS).

nkpf supports v1.58.2 and v1.88 firmware, and possibly others.

## Components

Each change that nkpf makes is a component.

| Component | Build | Function |
| --- | --- | --- |
| `rtc_stall_fix` | release and diagnostic | Prevents the stall after `BackLight Initializing... OK` |
| `diag` | diagnostic only | Shows interrupts and OAL IOCTL calls on the screen while WinCE runs |

### rtc_stall_fix

Without this patch, WinCE on the v1.58.2 device hangs after `BackLight Initializing... OK` on almost every boot and does not reach the desktop.

After the RTC driver sends `IOCTL_HAL_INIT_RTC`, each 1 ms OAL timer tick calls the RTC poll function, which advances an RTC read state machine by one step. A step that waits for the RTC ready bit polls it up to 15000 times. If the bit stays clear, the step returns without advancing, and the next tick polls again. Each tick then takes about 8.8 ms. The next timer interrupt is already pending when the handler returns, so the CPU goes straight back into the interrupt handler, and no thread ever runs.

`rtc_stall_fix` changes the first instruction of the RTC poll function to `bx lr`. On the v1.58.2 device, the state machine never read valid RTC data in any test. The effect on a device with a working RTC is unknown.

### diag

The diagnostic build copies a small monitor program, called the [resident](resident/), to `0x30198000`. It then hooks the NK interrupt handler and OEMIoControl, the OAL function that receives HAL IOCTL calls.

Every 512 interrupts, the resident draws a panel in the top-right corner:

| Row | Values |
| --- | --- |
| `irq` | Interrupt count, and the PC at which the last interrupt occurred |
| `sysintr` | The SYSINTR values (logical interrupt numbers) seen since boot. Values seen since the last update are white, the others gray |
| `ioctl` | OEMIoControl calls since boot, and how many of them set the SPI chip select of the CH374 USB driver |
| next rows | The last 6 other IOCTL calls, newest first: the slot number of the calling process, the IOCTL code, and the first two words of the input buffer |

In the IOCTL rows, `-` marks a word past the end of the input buffer, and `?` marks a value the resident cannot safely read.

## For developers

To add a component, define an `nkpf_component_t` in a new file, declare it in `nkpf.h`, add it to `components[]` in `main.c`, and add the object file to `NKPF_OBJS` in the `Makefile`.

## Building

```
make
make DIAG=1
```

The release build goes to `build/`, and the diagnostic build goes to `build-diag/`. 

To build aipc-boot with the diagnostic build of nkpf, run `make NKPF_DIAG=1` in the `baremetal/aipc-boot` directory.
