BAREMETAL_LIB = ../../lib
include $(BAREMETAL_LIB)/common.mk

OUT     = build

CFLAGS  = $(ARCHFLAGS) -Os -g -fomit-frame-pointer -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns \
          -ffunction-sections -fdata-sections -std=gnu11 \
          -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
          -I$(LIB) $(DEPFLAGS)
LDFLAGS = -T linker.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,--build-id=none -Wl,--no-warn-rwx-segments

OBJS    = $(OUT)/start.o $(OUT)/main.o

all: $(OUT)/sd-speed.elf

$(OUT)/%.o: %.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/%.o: %.S
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/sd-speed.elf: $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

clean:
	rm -rf $(OUT)

-include $(OBJS:.o=.d)

.PHONY: all clean
