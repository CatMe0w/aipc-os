BAREMETAL_LIB = ../../lib
include $(BAREMETAL_LIB)/common.mk

OUT     = build

CFLAGS  = $(ARCHFLAGS) -Os -fomit-frame-pointer -ffreestanding -fno-builtin \
          -ffunction-sections -fdata-sections -std=gnu11 \
          -Wall -Wextra -I$(LIB) $(DEPFLAGS)
LDFLAGS = -T linker.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,--build-id=none -Wl,--no-warn-rwx-segments

OBJS    = $(OUT)/start.o $(OUT)/main.o $(OUT)/lib_sd.o $(OUT)/lib_nand.o

all: $(OUT)/sd-nand.bin

$(OUT)/%.o: %.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/%.o: %.S
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/lib_%.o: $(LIB)/%.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/sd-nand.elf: $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(OUT)/sd-nand.bin: $(OUT)/sd-nand.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(OUT)

-include $(OBJS:.o=.d)

.PHONY: all clean
