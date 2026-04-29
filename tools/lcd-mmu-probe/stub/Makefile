CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -Wno-unused-parameter
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

STUBS    = lcd_nommu_red_probe lcd_mmu_green_probe lcd_mmu_dcache_blue_probe lcd_mmu_dcache_blit_probe lcd_mmu_dcache_blit_wait_probe
BINS     = $(addsuffix .bin,$(STUBS))
ENTRY    = start.o

all: $(BINS)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c lcd_probe_common.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.elf: %.o $(ENTRY)
	$(CC) $(LDFLAGS) -o $@ $(ENTRY) $<

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@ size: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
