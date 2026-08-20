HOST_OS := $(shell uname -s)

ifeq ($(origin CROSS), undefined)
  ifeq ($(HOST_OS),Darwin)
    # The Homebrew arm-none-eabi-gcc usually has no newlib, which LVGL and DOOM
    # need. Prefer the Arm GNU Toolchain bundle, the newest by directory name.
    DARWIN_CROSS_CANDIDATES := $(patsubst %gcc,%,$(wildcard /Applications/ArmGNUToolchain/*/arm-none-eabi/bin/arm-none-eabi-gcc))
    CROSS := $(lastword $(sort $(DARWIN_CROSS_CANDIDATES)))
  endif
  CROSS ?= arm-none-eabi-
endif

CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy

ARCHFLAGS = -mcpu=arm926ej-s -marm

# Objects from lib/ get a lib_ prefix. Two images then never trade object
# files, and each image compiles lib/ with its own flags.
LIB = $(BAREMETAL_LIB)
