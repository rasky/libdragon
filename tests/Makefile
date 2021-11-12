BUILD_DIR=build
include $(N64_INST)/include/n64.mk

all: testrom.z64 testrom_emu.z64

OBJS = 	${BUILD_DIR}/testrom.o \
		${BUILD_DIR}/test_constructors_cpp.o \
		${BUILD_DIR}/overlay0/md5.o \
		${BUILD_DIR}/overlay1/sha1.o

$(BUILD_DIR)/testrom.dfs: $(wildcard filesystem/*)
$(BUILD_DIR)/testrom.elf: ${OBJS}

testrom.z64: N64_ROM_TITLE="Libdragon Test ROM"
testrom.z64: $(BUILD_DIR)/testrom.dfs

$(BUILD_DIR)/testrom_emu.elf: ${OBJS}
testrom_emu.z64: N64_ROM_TITLE="Libdragon Test ROM"
testrom_emu.z64: $(BUILD_DIR)/testrom.dfs

${BUILD_DIR}/testrom_emu.o: testrom.c
	@mkdir -p $(dir $@)
	@echo "    [CC] $<"
	$(CC) -c $(CFLAGS) -DIN_EMULATOR=1 -o $@ $<

clean:
	rm -rf $(BUILD_DIR) testrom.z64 testrom_emu.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean
