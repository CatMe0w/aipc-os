CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -Werror
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

STUBS    = sd_baseline sd_init sd_pio_nodata sd_pio_withdata sd_l2 sd_write
BINS     = $(addsuffix .bin,$(STUBS))
ENTRY    = start.o

all: $(BINS)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

sd_pio_nodata.o: sd_pio.c
	$(CC) $(CFLAGS) -DPIO_WITHDATA=0 -c -o $@ $<

sd_pio_withdata.o: sd_pio.c
	$(CC) $(CFLAGS) -DPIO_WITHDATA=1 -c -o $@ $<

sd_l2.o: sd_pio.c
	$(CC) $(CFLAGS) -DPIO_WITHDATA=1 -DPIO_L2=1 -c -o $@ $<

%.elf: %.o $(ENTRY)
	$(CC) $(LDFLAGS) -o $@ $(ENTRY) $<

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@ size: $$(wc -c < $@) bytes"

sd_l2.bin: sd_l2.elf
	$(OBJCOPY) -O binary $< $@
	@test "$$(wc -c < $@)" -le 2496 || \
		{ echo "sd_l2.bin overlaps L2 buffer 6"; rm -f $@; exit 1; }
	@echo "$@ size: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
