BAREMETAL_LIB = ../lib
include $(BAREMETAL_LIB)/common.mk

LVGL    = third_party/lvgl

LOG_BASE = 0x301B0000u

CFLAGS  = $(ARCHFLAGS) -Os -fomit-frame-pointer \
          -ffunction-sections -fdata-sections \
          -nostartfiles -std=gnu11 \
          -Wall -Wextra -Wno-unused-parameter \
          -DLV_CONF_INCLUDE_SIMPLE -DLOG_BASE=$(LOG_BASE) \
          -I. -I$(LIB) -I../nkpf -Ithird_party

LDFLAGS = -T linker.ld -nostartfiles -Wl,--gc-sections \
          -Wl,--build-id=none -Wl,--no-warn-rwx-segments

# Shared drivers. See ../lib/README.md.
LIB_SRCS = soc.c log.c timer.c ch374.c mmu.c lcd.c sd.c fat.c ecc.c nand.c pf.c
LIB_OBJS = $(addprefix lib_,$(LIB_SRCS:.c=.o))

OBJS  = start.o lib_mmu_arm926.o handoff.o \
        main.o kbd.o boot.o ui.o syscalls.o eboot_patch.o panic.o nkpf_blobs.o \
        $(LIB_OBJS)

LVGL_SRCS = $(shell find $(LVGL)/src -name '*.c')
LVGL_OBJS = $(LVGL_SRCS:.c=.o)

IMAGE = BOOT.BIN

NKPF_DIAG ?= 0
NKPF_DIR = ../nkpf
ifeq ($(NKPF_DIAG),1)
NKPF_BIN = $(NKPF_DIR)/build-diag/nkpf.bin
else
NKPF_BIN = $(NKPF_DIR)/build/nkpf.bin
endif

all: $(IMAGE)

$(LVGL_SRCS): lvgl-check

lvgl-check:
	@test -f $(LVGL)/lvgl.h || \
	  (echo "third_party/lvgl is empty; run: git submodule update --init $(LVGL)" && false)

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(NKPF_BIN): FORCE
	$(MAKE) -C $(NKPF_DIR) DIAG=$(NKPF_DIAG)

# Always rebuilt, because NKPF_DIAG switches the blob behind the same object.
nkpf_blobs.o: nkpf_blobs.S $(NKPF_BIN) FORCE
	$(CC) $(CFLAGS) -DNKPF_BIN='"$(NKPF_BIN)"' -c -o $@ $<

lib_%.o: $(LIB)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

lib_%.o: $(LIB)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

aipc-boot.elf: $(OBJS) $(LVGL_OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LVGL_OBJS) -lm

$(IMAGE): aipc-boot.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$(IMAGE): $$(wc -c < $@) bytes"

clean:
	rm -f *.o aipc-boot.elf $(IMAGE)
	rm -f $(LVGL_OBJS)

FORCE:

.PHONY: all clean lvgl-check FORCE
