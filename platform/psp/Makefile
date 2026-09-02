# redch3psp full-engine PSP reconstruction target
# Invoked from repository root by GitHub Actions.

.DEFAULT_GOAL := all

ROOT ?= $(CURDIR)
SOURCE_TREE ?= $(ROOT)/work/re3
BUILD_DIR := $(ROOT)/build/psp10a

TARGET := $(BUILD_DIR)/GTA3PSP

APP_CPP := $(shell find "$(SOURCE_TREE)/src" -type f -name '*.cpp' \
	! -path '*/skel/glfw/*' \
	! -path '*/skel/win/*' \
	! -path '*/audio/oal/*' \
	! -path '*/audio/eax/*' \
	! -name 'sampman_miles.cpp' \
	! -name 'sampman_oal.cpp')

RW_CPP := $(shell find "$(SOURCE_TREE)/vendor/librw/src" -maxdepth 1 -type f -name '*.cpp') \
	$(SOURCE_TREE)/vendor/librw/src/lodepng/lodepng.cpp \
	$(SOURCE_TREE)/vendor/librw/src/psp/psp.cpp

APP_OBJS := $(patsubst $(SOURCE_TREE)/%.cpp,$(BUILD_DIR)/upstream/%.o,$(APP_CPP))
RW_OBJS  := $(patsubst $(SOURCE_TREE)/%.cpp,$(BUILD_DIR)/upstream/%.o,$(RW_CPP))
PSP_OBJS := $(BUILD_DIR)/local/platform/psp/psp_platform.o
OBJS := $(APP_OBJS) $(RW_OBJS) $(PSP_OBJS)

SRC_INC_DIRS := src src/animation src/audio src/buildings src/collision src/control \
	src/core src/entities src/math src/modelinfo src/objects src/peds src/render \
	src/rw src/save src/skel src/text src/vehicles src/weapons src/extras src/fakerw

INCDIRS := $(foreach d,$(SRC_INC_DIRS),-I$(SOURCE_TREE)/$(d)) \
	-I$(SOURCE_TREE)/vendor/librw \
	-I$(SOURCE_TREE)/vendor/librw/src \
	-I$(SOURCE_TREE)/vendor/librw/src/psp \
	-I$(ROOT)/platform/psp

DEFINES := -DPSP -DPSP_PORT -DRW_PSP -DLIBRW -DMASTER -DFINAL -DFIX_BUGS -DNO_MOVIES \
	-DDEFAULT_SCREEN_WIDTH=480 -DDEFAULT_SCREEN_HEIGHT=272

CFLAGS := -O2 -G0 -Wall -Wno-unused-parameter -Wno-unused-variable \
	-fno-strict-aliasing -ffast-math -fsingle-precision-constant $(DEFINES) $(INCDIRS)
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti -std=gnu++11
ASFLAGS := $(CFLAGS)

LIBS := -lpspgu -lpspgum -lpspdisplay -lpspctrl -lpsprtc -lpsppower -lm
LDFLAGS := -Wl,-Map,$(BUILD_DIR)/GTA3PSP.map

BUILD_PRX = 1
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = GTA III PSP re3 10A

.PHONY: prepare
prepare:
	@mkdir -p "$(BUILD_DIR)"

$(BUILD_DIR)/upstream/%.o: $(SOURCE_TREE)/%.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/local/%.o: $(ROOT)/%.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJS): | prepare

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
