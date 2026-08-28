CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -ffunction-sections -fdata-sections -Wall -Wextra
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

TARGETS  = timer-semantics timer-wrap timer-load

all: $(TARGETS:%=%.bin)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

timer-semantics.o: timer_probe.c
	$(CC) $(CFLAGS) -c -o $@ $<

timer-wrap.o: wrap_probe.c
	$(CC) $(CFLAGS) -c -o $@ $<

timer-load.o: load_probe.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.elf: start.o %.o linker.ld
	$(CC) $(LDFLAGS) -o $@ start.o $*.o

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
