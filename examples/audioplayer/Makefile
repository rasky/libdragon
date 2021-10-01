BUILD_DIR=build
include n64.mk

src = audioplayer.c
assets = $(wildcard assets/*.xm)
assets_conv = $(addprefix filesystem/,$(notdir $(assets:%.xm=%.xm64)))

all: audioplayer.z64

# Run audioconv on all XM files under assets/
# We do this file by file, but we could even do it just once for the whole
# directory, becuase audioconv64 supports directory walking.
filesystem/%.xm64: assets/%.xm
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@audioconv64 -o filesystem $<

$(BUILD_DIR)/audioplayer.dfs: $(assets_conv)
$(BUILD_DIR)/audioplayer.elf: $(src:%.c=$(BUILD_DIR)/%.o)

audioplayer.z64: N64_ROM_TITLE="Audio Player"
audioplayer.z64: $(BUILD_DIR)/audioplayer.dfs

clean:
	rm -rf $(BUILD_DIR) audioplayer.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean
