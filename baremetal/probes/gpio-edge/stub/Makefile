CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding \
	   -fno-builtin -nostdlib -nostartfiles -ffunction-sections \
	   -fdata-sections -Wall -Wextra
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

all: gpio-edge.bin

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

gpio_edge_probe.o: gpio_edge_probe.c
	$(CC) $(CFLAGS) -c -o $@ $<

gpio-edge.elf: start.o gpio_edge_probe.o linker.ld
	$(CC) $(LDFLAGS) -o $@ start.o gpio_edge_probe.o

gpio-edge.bin: gpio-edge.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
