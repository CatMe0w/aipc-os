CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -O2 -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

TARGET   = probe
OBJS     = start.o probe.o

all: $(TARGET).bin

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "probe size: $$(wc -c < $@) bytes"

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin

.PHONY: all clean
