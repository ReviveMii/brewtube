ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPRO)/libogc2/wii_rules
include sources.mk

.DEFAULT_GOAL := all

OUT := $(CURDIR)/../../build_ffmpeg
CFLAGS := -O3 -std=gnu99 -w -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-int-conversion -ffast-math -fno-math-errno -fno-signed-zeros $(MACHDEP) -DHAVE_AV_CONFIG_H -I$(CURDIR) -I$(LIBOGC_INC)
LIBNAMES := avutil avcodec avformat

define LIBRULE
$(OUT)/lib$(1).a: $$(patsubst %.c,$(OUT)/%.o,$$(filter lib$(1)/%,$$(FFSRC)))
	@rm -f $$@
	$(AR) rcs $$@ $$^
endef

$(foreach l,$(LIBNAMES),$(eval $(call LIBRULE,$(l))))

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

all: $(LIBNAMES:%=$(OUT)/lib%.a)

clean:
	@rm -rf $(OUT)

.PHONY: all clean
