HOST_OS   := $(shell uname -s)

# Prefer the Arm GNU Toolchain app bundle on macOS because Homebrew's
# arm-none-eabi-gcc typically lacks newlib. We do not parse the vendor's
# version string explicitly; instead, scan installed toolchain prefixes and
# pick the newest-looking path by directory name. Users can still override
# CROSS=... on the command line.
ifeq ($(origin CROSS), undefined)
  ifeq ($(HOST_OS),Darwin)
    DARWIN_CROSS_CANDIDATES := $(patsubst %gcc,%,$(wildcard /Applications/ArmGNUToolchain/*/arm-none-eabi/bin/arm-none-eabi-gcc))
    CROSS := $(lastword $(sort $(DARWIN_CROSS_CANDIDATES)))
  endif

  CROSS ?= arm-none-eabi-
endif

CC        = $(CROSS)gcc
OBJCOPY   = $(CROSS)objcopy

DOOMGENERIC_DIR = doomgeneric/doomgeneric

# WAD version selection.  Pass WAD=<version> on the command line.
# Supported values and the WAD file to place in doom/wad/:
#   shareware  (default) - DOOM1.WAD  - shareware Doom 1, included in repo
#   doom1                - doom.wad   - retail Doom 1 (user-provided)
#   doom2                - doom2.wad  - Doom II (user-provided)
#   tnt                  - tnt.wad    - Final Doom: TNT Evilution (user-provided)
#   plutonia             - plutonia.wad - Final Doom: Plutonia Experiment (user-provided)
WAD ?= shareware

ifeq ($(WAD),shareware)
  WAD_FILENAME   := doom1.wad
  WAD_FILE       := wad/DOOM1.WAD
  TARGET_SUFFIX  :=
else ifeq ($(WAD),doom1)
  WAD_FILENAME   := doom.wad
  WAD_FILE       := wad/DOOM.WAD
  TARGET_SUFFIX  := -doom1
else ifeq ($(WAD),doom2)
  WAD_FILENAME   := doom2.wad
  WAD_FILE       := wad/DOOM2.WAD
  TARGET_SUFFIX  := -doom2
else ifeq ($(WAD),tnt)
  WAD_FILENAME   := tnt.wad
  WAD_FILE       := wad/TNT.WAD
  TARGET_SUFFIX  := -tnt
else ifeq ($(WAD),plutonia)
  WAD_FILENAME   := plutonia.wad
  WAD_FILE       := wad/PLUTONIA.WAD
  TARGET_SUFFIX  := -plutonia
else
  $(error Unknown WAD version "$(WAD)". Supported: shareware doom1 doom2 tnt plutonia)
endif

CFLAGS  = -mcpu=arm926ej-s -marm -O2 -fomit-frame-pointer \
          -nostartfiles \
          -Wall -Wextra -Wno-unused-parameter \
          -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
          -DWAD_FILENAME=\"$(WAD_FILENAME)\" \
          -I$(DOOMGENERIC_DIR)

# -nostartfiles: we provide our own _start; newlib (libc/libm/libgcc) links in
# automatically via arm-none-eabi defaults.
LDFLAGS = -T linker.ld -nostartfiles -Wl,--gc-sections

TARGET  = doom$(TARGET_SUFFIX)

SRCS_S  = src/start.S \
          src/mmu_arm926.S
SRCS_C  = src/doomgeneric_aipc.c \
          src/mmu_table.c \
          src/syscalls.c

# Engine sources from doomgeneric.  Exclude:
#   doomgeneric_*.c  - platform implementations (we provide doomgeneric_aipc.c)
#   i_allegromusic.c, i_allegrosound.c - require Allegro
#   i_sdlmusic.c, i_sdlsound.c        - require SDL
#   i_cdmus.c                          - CD audio, not applicable
DOOM_SRCS = $(filter-out \
    $(wildcard $(DOOMGENERIC_DIR)/doomgeneric_*.c) \
    $(DOOMGENERIC_DIR)/i_allegromusic.c \
    $(DOOMGENERIC_DIR)/i_allegrosound.c \
    $(DOOMGENERIC_DIR)/i_sdlmusic.c \
    $(DOOMGENERIC_DIR)/i_sdlsound.c \
    $(DOOMGENERIC_DIR)/i_cdmus.c, \
    $(wildcard $(DOOMGENERIC_DIR)/*.c))

OBJS = $(SRCS_S:.S=.o) \
       $(SRCS_C:.c=.o) \
       $(DOOM_SRCS:%.c=%.o)

all: $(TARGET).bin

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "$(TARGET).bin: $$(wc -c < $@) bytes"

clean:
	rm -f src/*.o $(TARGET).elf $(TARGET).bin
	rm -f $(DOOMGENERIC_DIR)/*.o

cleanall:
	rm -f src/*.o $(DOOMGENERIC_DIR)/*.o
	rm -f doom.elf doom.bin doom-doom1.elf doom-doom1.bin
	rm -f doom-doom2.elf doom-doom2.bin doom-tnt.elf doom-tnt.bin
	rm -f doom-plutonia.elf doom-plutonia.bin

.PHONY: all clean cleanall
