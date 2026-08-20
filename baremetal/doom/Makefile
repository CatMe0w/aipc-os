BAREMETAL_LIB = ../lib
include $(BAREMETAL_LIB)/common.mk

DOOMGENERIC_DIR = doomgeneric/doomgeneric

# WAD version. Pass WAD=<version> on the command line. Each version needs its
# WAD file in doom/wad/:
#   shareware (default)  DOOM1.WAD     shareware Doom 1, in the repo
#   doom1                DOOM.WAD      retail Doom 1, user supplied
#   doom2                DOOM2.WAD     Doom II, user supplied
#   tnt                  TNT.WAD       Final Doom: TNT Evilution, user supplied
#   plutonia             PLUTONIA.WAD  Final Doom: Plutonia, user supplied
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

# Next free slot of the shared log pool, see ../README.md.
LOG_BASE = 0x301A0000u

CFLAGS  = $(ARCHFLAGS) -O2 -fomit-frame-pointer \
          -ffunction-sections -fdata-sections \
          -nostartfiles \
          -DLOG_BASE=$(LOG_BASE) \
          -Wall -Wextra -Wno-unused-parameter \
          -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
          -DWAD_FILENAME=\"$(WAD_FILENAME)\" \
          -I$(DOOMGENERIC_DIR) -I$(LIB)

# -nostartfiles, because src/start.S has our own _start. The arm-none-eabi
# defaults still link newlib (libc, libm, libgcc).
LDFLAGS = -T linker.ld -nostartfiles -Wl,--gc-sections

TARGET  = doom$(TARGET_SUFFIX)

SRCS_S  = src/start.S
SRCS_C  = src/doomgeneric_aipc.c \
          src/ch374_keyboard.c \
          src/syscalls.c

# Shared drivers. See ../lib/README.md.
LIB_SRCS = soc.c log.c timer.c ch374.c mmu.c lcd.c
LIB_OBJS = $(addprefix src/lib_,$(LIB_SRCS:.c=.o)) src/lib_mmu_arm926.o

# Engine sources from doomgeneric. Excluded:
#   doomgeneric_*.c                    - other platforms, we have our own
#   i_allegromusic.c, i_allegrosound.c - need Allegro
#   i_sdlmusic.c, i_sdlsound.c         - need SDL
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
       $(LIB_OBJS) \
       $(DOOM_SRCS:%.c=%.o)

all: $(TARGET).bin

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/lib_%.o: $(LIB)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/lib_%.o: $(LIB)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "$(TARGET).bin: $$(wc -c < $@) bytes"

clean:
	rm -f src/*.o $(LIB_OBJS) $(TARGET).elf $(TARGET).bin
	rm -f $(DOOMGENERIC_DIR)/*.o

cleanall:
	rm -f src/*.o $(DOOMGENERIC_DIR)/*.o
	rm -f doom.elf doom.bin doom-doom1.elf doom-doom1.bin
	rm -f doom-doom2.elf doom-doom2.bin doom-tnt.elf doom-tnt.bin
	rm -f doom-plutonia.elf doom-plutonia.bin

.PHONY: all clean cleanall
