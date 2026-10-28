BAREMETAL_LIB = ../lib
include $(BAREMETAL_LIB)/common.mk

OUT   = build
LVGL  = third_party/lvgl
FATFS = third_party/fatfs

LOG_BASE = 0x301B0000u

CFLAGS  = $(ARCHFLAGS) -Os -fomit-frame-pointer \
          -ffunction-sections -fdata-sections -std=gnu11 \
          -Wall -Wextra -Wno-unused-parameter \
          -DLV_CONF_INCLUDE_SIMPLE -DLOG_BASE=$(LOG_BASE) \
          -I. -I$(LIB) -Ithird_party -I$(FATFS) $(DEPFLAGS)
LDFLAGS = -T linker.ld -nostartfiles -Wl,--gc-sections \
          -Wl,--build-id=none -Wl,--no-warn-rwx-segments

SRCS = start.S handoff.S cfw_blob.S \
       main.c kbd.c boot.c sysinfo.c battery.c power.c \
       ui.c ui_modal.c ui_launch.c ui_pages.c fade.c \
       storage.c card.c ini.c entries.c settings.c payload.c \
       $(wildcard fonts/*.c) $(FATFS)/ff.c $(FATFS)/ffunicode.c

# Shared drivers. See ../lib/README.md.
LIB_SRCS = soc.c log.c timer.c ch374.c mmu.c mmu_arm926.S lcd.c sd.c \
           ecc.c nand.c

LVGL_SRCS = $(shell find $(LVGL)/src -name '*.c' 2>/dev/null)

OBJS = $(patsubst %,$(OUT)/%.o,$(basename $(SRCS))) \
       $(patsubst %,$(OUT)/lib/%.o,$(basename $(LIB_SRCS)))
LVGL_OBJS = $(patsubst %.c,$(OUT)/%.o,$(LVGL_SRCS))

# Thumb code is about 20% smaller. The drivers and start.S stay ARM.
$(LVGL_OBJS): ARCHFLAGS += -mthumb

# Built by ../cfw. Run make in baremetal/ to build both.
CFW_BIN = ../cfw/build/cfw.bin

# The version on the Info page. The release workflow sets the release tag.
AIPC_BOOT_VERSION ?= dev

all: $(OUT)/BOOT.BIN

ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifeq ($(wildcard $(LVGL)/lvgl.h),)
$(error $(LVGL) is empty. Run: git submodule update --init $(LVGL))
endif
endif

$(OUT)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/lib/%.o: $(LIB)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/lib/%.o: $(LIB)/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/sysinfo.o: CFLAGS += -DAIPC_BOOT_VERSION='"$(AIPC_BOOT_VERSION)"'

$(OUT)/cfw_blob.o: CFLAGS += -DCFW_BIN='"$(CFW_BIN)"'
$(OUT)/cfw_blob.o: $(CFW_BIN)

# A cfw.bin that is missing, or older than the interface header, would not
# match this build. Stop, because only baremetal/Makefile builds CFW.
$(CFW_BIN): $(LIB)/cfw_abi.h
	@echo "$@ is missing or stale. Run make in baremetal/." >&2; false

$(OUT)/aipc-boot.elf: $(OBJS) $(LVGL_OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LVGL_OBJS)

$(OUT)/BOOT.BIN: $(OUT)/aipc-boot.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

clean:
	rm -rf $(OUT)

-include $(OBJS:.o=.d) $(LVGL_OBJS:.o=.d)

.PHONY: all clean
