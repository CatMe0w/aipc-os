# lib

Drivers shared by the images under `baremetal/`.

| Source | What it is | Used by |
| --- | --- | --- |
| `soc.c` | UART, L2 buffer and NAND controller bring-up, handoff to EBOOT | all |
| `log.c` | Line log to a DDR window and the UART | all |
| `timer.c` | Timer2, millisecond clock and delay | aipc-boot, DOOM |
| `ch374.c` | Internal keyboard over the CH374 USB host bridge | aipc-boot, DOOM |
| `lcd.c` | Panel bring-up, 800x480 RGB565 | aipc-boot, DOOM |
| `mmu.c`, `mmu_arm926.S` | Section table, cache and write buffer control | aipc-boot, DOOM |
| `sd.c`, `fat.c` | SD card and read-only FAT16/FAT32 | aipc-boot, openNBOOT |
| `nand.c`, `ecc.c` | NAND reads and the 4x528 interleaved ECC layout | aipc-boot, openNBOOT |

## Building

Include `common.mk` first. It finds the toolchain and sets `ARCHFLAGS`. Then set the `CFLAGS` of the image:

```make
BAREMETAL_LIB = ../lib
include $(BAREMETAL_LIB)/common.mk

LIB_SRCS = soc.c log.c timer.c
LIB_OBJS = $(addprefix lib_,$(LIB_SRCS:.c=.o))

lib_%.o: $(LIB)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
```

The `lib_` prefix separates these object files from the ones that the image builds itself. In a build directory it also shows which objects came from here.
