CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -ffunction-sections -fdata-sections -Wall -Wextra
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

TARGETS  = cpusrc-measure cpusrc-low cpusrc-high

all: $(TARGETS:%=%.bin)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

cpusrc-measure.o: measure_probe.c cpufreq_common.h
	$(CC) $(CFLAGS) -c -o $@ $<

cpusrc-low.o: switch_probe.c cpufreq_common.h
	$(CC) $(CFLAGS) -DUSE_PLL1=0 -c -o $@ $<

cpusrc-high.o: switch_probe.c cpufreq_common.h
	$(CC) $(CFLAGS) -DUSE_PLL1=1 -c -o $@ $<

%.elf: start.o %.o linker.ld
	$(CC) $(LDFLAGS) -o $@ start.o $*.o

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
