CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -ffunction-sections -fdata-sections -Wall -Wextra
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

TARGETS  = touchpad-passive touchpad-enable touchpad-identify touchpad-poll touchpad-init touchpad-uart-query touchpad-uart-motion touchpad-uart-irq touchpad-gpio4

all: $(TARGETS:%=%.bin)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

touchpad-uart-query.o: touchpad_uart.c touchpad_probe.c
	$(CC) $(CFLAGS) -DUART_MOTION=0 -c -o $@ $<

touchpad-uart-motion.o: touchpad_uart.c touchpad_probe.c
	$(CC) $(CFLAGS) -DUART_MOTION=1 -c -o $@ $<

touchpad-uart-irq.o: touchpad_uart.c touchpad_probe.c
	$(CC) $(CFLAGS) -DUART_MOTION=0 -DUART_IRQ_CHECK=1 -c -o $@ $<

touchpad-gpio4.o: touchpad_gpio4.c
	$(CC) $(CFLAGS) -c -o $@ $<

touchpad-passive.o: touchpad_probe.c
	$(CC) $(CFLAGS) -DPROBE_ACTIVE=0 -DPROBE_DIAGNOSTIC=0 -DPROBE_POLL=0 -DPROBE_INIT=0 -c -o $@ $<

touchpad-enable.o: touchpad_probe.c
	$(CC) $(CFLAGS) -DPROBE_ACTIVE=1 -DPROBE_DIAGNOSTIC=0 -DPROBE_POLL=0 -DPROBE_INIT=0 -c -o $@ $<

touchpad-identify.o: touchpad_probe.c
	$(CC) $(CFLAGS) -DPROBE_ACTIVE=1 -DPROBE_DIAGNOSTIC=1 -DPROBE_POLL=0 -DPROBE_INIT=0 -c -o $@ $<

touchpad-poll.o: touchpad_probe.c
	$(CC) $(CFLAGS) -DPROBE_ACTIVE=1 -DPROBE_DIAGNOSTIC=0 -DPROBE_POLL=1 -DPROBE_INIT=0 -c -o $@ $<

touchpad-init.o: touchpad_probe.c
	$(CC) $(CFLAGS) -DPROBE_ACTIVE=1 -DPROBE_DIAGNOSTIC=0 -DPROBE_POLL=0 -DPROBE_INIT=1 -c -o $@ $<

%.elf: start.o %.o linker.ld
	$(CC) $(LDFLAGS) -o $@ start.o $*.o -lgcc

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
