CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -O2 -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

PROBES   = scan_probe diag_probe delay_probe load_probe load2_probe temp_probe cond_probe

# delay_probe and temp_probe take their round count at build time. A short
# build answers whether the probe works; a long one collects the data.
ROUNDS  ?= 256
delay_probe.o temp_probe.o: CFLAGS += -DROUNDS=$(ROUNDS)u

# delay_probe also builds with the delays in reverse order. Running both tells
# an effect that follows the delay value from one that follows the position of
# the delay inside a round.
all: $(PROBES:%=%.bin) delay_rev.bin

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

delay_rev.o: delay_probe.c
	$(CC) $(CFLAGS) -DREVERSE_DELAYS -c -o $@ $<

%.elf: start.o %.o linker.ld
	$(CC) $(LDFLAGS) -o $@ start.o $*.o

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
