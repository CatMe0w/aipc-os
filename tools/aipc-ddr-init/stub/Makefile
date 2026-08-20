CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -ffreestanding -nostdlib -nostartfiles \
           -Wall -Wextra
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

TARGETS  = ddr_init_v1_58_2 ddr_init_v1_88

all: $(TARGETS:%=%.bin)

%.o: %.S ddr_init_common.inc
	$(CC) $(CFLAGS) -c -o $@ $<

%.elf: %.o
	$(CC) $(LDFLAGS) -o $@ $<

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

clean:
	rm -f $(TARGETS:%=%.o) $(TARGETS:%=%.elf) $(TARGETS:%=%.bin)

.PHONY: all clean
