CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -std=gnu11

LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

OBJS     = start.o resume.o bp.o step.o main.o musb.o rsp.o trace.o

# One object set, two link addresses. BOOT.BIN is for openNBOOT, which loads SD
# payloads to 0x33000000. aipc-boot runs at that same address, so the copy that
# it loads links above the aipc-boot stack, below the framebuffer.
AIPC_BOOT_BASE = 0x33A00000

all: BOOT.BIN gdbstub.bin

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

gdbstub.elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# `make gdbstub-0x<addr>.bin` relocates the image, so that a running stub can
# load and start the next one. Never rename such a build to BOOT.BIN. From the
# card it would run at the wrong address. The name carries the base address, so
# a stale build cannot look like a fresh one.
gdbstub-0x%.elf: $(OBJS)
	$(CC) $(LDFLAGS) -Xlinker --defsym=__base=0x$* -o $@ $^

BOOT.BIN: gdbstub.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes (base 0x33000000)"

gdbstub.bin: gdbstub-$(AIPC_BOOT_BASE).elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes (base $(AIPC_BOOT_BASE))"

gdbstub-0x%.bin: gdbstub-0x%.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes (base 0x$*)"

clean:
	rm -f $(OBJS) gdbstub.elf BOOT.BIN gdbstub.bin gdbstub-0x*.elf gdbstub-0x*.bin

.PHONY: all clean
