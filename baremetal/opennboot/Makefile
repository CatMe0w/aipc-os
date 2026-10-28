BAREMETAL_LIB = ../lib
include $(BAREMETAL_LIB)/common.mk

CFLAGS   = $(ARCHFLAGS) -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -ffunction-sections -fdata-sections \
           -nostartfiles -Wall -Wextra -Wno-unused-parameter -std=c11 \
           -I. -I$(LIB) $(DEPFLAGS)
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections -Wl,--build-id=none \
           -Wl,--no-warn-rwx-segments

LIB_SRCS = log.c soc.c ecc.c nand.c sd.c fat.c
LIB_OBJS = $(addprefix lib_,$(LIB_SRCS:.c=.o))

OBJS     = start.o main.o $(LIB_OBJS)

HOSTCC      ?= cc
HOSTCFLAGS   = -Wall -Wextra -std=c11 -O1 -g

all: opennboot.bin bootbin test

bootbin:
	$(MAKE) -C test/bootbin CROSS=$(CROSS)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

lib_%.o: $(LIB)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

opennboot.elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

opennboot.bin: opennboot.elf
	$(OBJCOPY) -O binary $< $@
	@size=$$(wc -c < $@); \
	 echo "$@ size: $$size bytes ($$(( ($$size + 2047) / 2048 )) pages of 63)"

test: test/test_ecc
	./test/test_ecc

test/test_ecc: test/test_ecc.c $(LIB)/ecc.c $(LIB)/ecc.h
	$(HOSTCC) $(HOSTCFLAGS) -I$(LIB) -o $@ test/test_ecc.c $(LIB)/ecc.c

clean:
	rm -f *.o *.d *.elf *.bin test/test_ecc
	$(MAKE) -C test/bootbin clean

-include $(OBJS:.o=.d)

.PHONY: all clean test bootbin
