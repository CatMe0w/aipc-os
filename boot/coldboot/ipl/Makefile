CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -Wno-unused-parameter -std=c11
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections -Wl,--build-id=none

OBJS     = start.o main.o log.o nand.o sd.o fat.o

all: ipl.bin

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

ipl.elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

ipl.bin: ipl.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@ size: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
