CROSS    ?= arm-none-eabi-
CC        = $(CROSS)gcc
OBJCOPY   = $(CROSS)objcopy

DOOMGENERIC_DIR = doomgeneric/doomgeneric

CFLAGS  = -mcpu=arm926ej-s -marm -O2 -fomit-frame-pointer \
          -nostartfiles \
          -Wall -Wextra -Wno-unused-parameter \
          -I$(DOOMGENERIC_DIR)

# -nostartfiles: we provide our own _start; newlib (libc/libm/libgcc) links in
# automatically via arm-none-eabi defaults.
LDFLAGS = -T linker.ld -nostartfiles -Wl,--gc-sections

TARGET  = doom

SRCS_S  = src/start.S
SRCS_C  = src/doomgeneric_aipc.c \
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
	@echo "doom.bin: $$(wc -c < $@) bytes"

clean:
	rm -f src/*.o $(TARGET).elf $(TARGET).bin
	rm -f $(DOOMGENERIC_DIR)/*.o

.PHONY: all clean
