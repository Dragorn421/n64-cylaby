# SPDX-FileCopyrightText: 2026 Dragorn421
# SPDX-License-Identifier: CC0-1.0

ROMNAME := cylaby

BUILD_DIR = build

ifeq ($(N64_GCCPREFIX),)
  ifeq ($(N64_INST),)
    $(error Neither N64_GCCPREFIX nor N64_INST is set.)
  else
    N64_GCCPREFIX := $(N64_INST)
  endif
endif
N64_INST := ./build/libdragon
$(info Using N64_GCCPREFIX = $(N64_GCCPREFIX))
$(info Using N64_INST = $(N64_INST))

ifeq ($(wildcard $(N64_INST)/include/n64.mk),)
  $(error Run build_libdragon.sh before make.)
endif
include $(N64_INST)/include/n64.mk

#N64_C_AND_CXX_FLAGS += -Og

N64_C_AND_CXX_FLAGS += -Iinclude

all: $(ROMNAME).z64
.PHONY: all

C_FILES = $(shell find src -name '*.c') 
OBJS = $(addprefix $(BUILD_DIR)/,$(C_FILES:.c=.o))

$(BUILD_DIR)/$(ROMNAME).elf: $(OBJS)

$(ROMNAME).z64: N64_ROM_TITLE = "cylaby"

clean:
	$(RM) -r $(BUILD_DIR) *.z64
.PHONY: clean

-include $(shell find $(BUILD_DIR) -name '*.d')
