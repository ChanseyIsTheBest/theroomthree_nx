#---------------------------------------------------------------------------------
# THE ROOM THREE -- Nintendo Switch homebrew loader
# Fireproof Games / Unity 2021.3.8f1 / IL2CPP / arm64
#
# Requires devkitA64 plus these devkitPro portlibs:
#   pacman -S switch-dev
#   pacman -S switch-mesa switch-libdrm_nouveau switch-sdl2 switch-libpng switch-zlib
#
# Every .c under source/ is compiled automatically, so adding a file needs no
# edit here. nx_patch_r3.h and unity_entrypoints.h are headers included by main.c.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO in your environment. (export DEVKITPRO=/opt/devkitpro)")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := theroom3_nx
APP_TITLE   := The Room Three
APP_AUTHOR  := ChanseyIsTheBest
APP_VERSION := 1.0.0

# Icon: ours if present, else libnx's default, else no icon at all.
# elf2nro aborts with "Failed to open input icon!" if --icon points at a file
# that does not exist, so the flag is never passed unconditionally.
# Replace icon.jpg with your own any time -- 256x256 JPEG, RGB (not CMYK, not
# progressive), which is what the NRO asset section requires.
ifneq ($(wildcard $(TOPDIR)/icon.jpg),)
    APP_ICON := $(TOPDIR)/icon.jpg
else ifneq ($(wildcard $(LIBNX)/default_icon.jpg),)
    APP_ICON := $(LIBNX)/default_icon.jpg
endif
export APP_TITLE APP_AUTHOR APP_VERSION APP_ICON

BUILD    := build
SOURCES  := source
INCLUDES := source

ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) \
           $(INCLUDE) -D__SWITCH__
CFLAGS  += -DLOAD_ADDRESS=0xC0000000
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS := -g $(ARCH)
# --wrap routes allocations into the GPU arena in gpu_arena.c, and the nwindow
# calls through the buffer-queue logging in imports.c. THIS LIST AND THOSE
# FUNCTIONS MUST AGREE: a --wrap with no __wrap_ function is an
# undefined-reference error out of libEGL.a (mesa calls malloc/free too), and a
# __wrap_ function with no --wrap is silently dead code.
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) \
           -Wl,--wrap,free -Wl,--wrap,malloc -Wl,--wrap,memalign -Wl,--wrap,calloc \
           -Wl,--wrap,realloc -Wl,--wrap,memmove -Wl,--wrap,memcpy \
           -Wl,--wrap,nwindowDequeueBuffer -Wl,--wrap,nwindowQueueBuffer \
           -Wl,--wrap,nwindowCancelBuffer

# ---- ffmpeg, for the ending cutscenes (r3_video.c) -------------------------
# The Room Three plays its four endings through Handheld.PlayFullScreenMovie,
# which on Android is handed to android.media.MediaPlayer. There is no
# MediaPlayer here, so the clips are decoded with ffmpeg instead.
#
#     sudo dkp-pacman -S switch-ffmpeg
#
# Set R3_VIDEO to 0 in source/config.h to drop the feature: r3_video.c compiles
# to empty stubs needing no ffmpeg header and no ffmpeg library, FFMPEG_LIBS
# becomes dead weight the linker discards, and the endings are skipped as they
# were before. Nothing else in the port uses ffmpeg.
R3_VIDEO_ON := $(shell grep -qE '^\s*#define\s+R3_VIDEO\s+1' $(TOPDIR)/source/config.h && echo 1)
ifeq ($(R3_VIDEO_ON),1)
  ifeq ($(wildcard $(DEVKITPRO)/portlibs/switch/include/libavcodec/avcodec.h),)
    $(warning ==============================================================)
    $(warning  switch-ffmpeg not found. Install it with:)
    $(warning     sudo dkp-pacman -S switch-ffmpeg)
    $(warning  Or build without the ending cutscenes: set R3_VIDEO to 0 in)
    $(warning  source/config.h.)
    $(warning ==============================================================)
    $(error switch-ffmpeg missing -- see above)
  endif
endif

PKGCONF     := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
FFMPEG_PKGS := libavformat libavcodec libswresample libswscale libavutil
FFMPEG_LIBS := $(shell $(PKGCONF) --static --libs $(FFMPEG_PKGS) 2>/dev/null)
ifeq ($(strip $(FFMPEG_LIBS)),)
FFMPEG_LIBS := -lavformat -lavcodec -lswresample -lswscale -lavutil \
               -ldav1d -lass -lfribidi -lharfbuzz -lfreetype -lbz2
endif

# mesa GLES3 + EGL + nouveau, SDL2 for window/HID/audio, libpng for the optional
# cursor.png (nx_pointer.c), zlib. -lpng must precede -lz: libpng calls into it.
# ffmpeg goes in a --start-group: the codec libraries reference each other both
# ways and a single pass does not resolve them.
LIBS := -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau \
        -Wl,--start-group $(FFMPEG_LIBS) -Wl,--end-group \
        -lpng -lbz2 -lz -lnx -lm

LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD := $(CXX)
export OFILES := $(addsuffix .o,$(SFILES)) $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(PORTLIBS)/include/SDL2 -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
$(BUILD):
	@mkdir -p $@
clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
DEPENDS := $(OFILES:.o=.d)
NROFLAGS := --nacp=$(OUTPUT).nacp
ifneq ($(strip $(APP_ICON)),)
    NROFLAGS += --icon=$(APP_ICON)
endif
all : $(OUTPUT).nro
$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf : $(OFILES)
-include $(DEPENDS)
endif
