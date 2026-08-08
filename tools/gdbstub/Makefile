CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -std=gnu11

# BASE relocates the image so a running stub can load and start the next one.
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections \
           $(if $(BASE),-Xlinker --defsym=__base=$(BASE))

OBJS     = start.o resume.o bp.o step.o main.o musb.o rsp.o trace.o

# A relocated build must not be called BOOT.BIN. Boot such build from the card
# would run at the wrong address and lead to unexpected behavior.
# The elf name includes the base to avoid stale builds.
ELF      = $(if $(BASE),gdbstub-$(BASE).elf,gdbstub.elf)
IMAGE    = $(if $(BASE),gdbstub-$(BASE).bin,BOOT.BIN)

all: $(IMAGE)

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(ELF): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(IMAGE): $(ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "$(IMAGE): $$(wc -c < $@) bytes"

clean:
	rm -f $(OBJS) gdbstub.elf BOOT.BIN gdbstub-0x*.elf gdbstub-0x*.bin

.PHONY: all clean
