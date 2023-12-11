#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint32_t pi_dom1_config;
    uint32_t clock_rate;
    uint32_t boot_address;
    uint32_t sdk_version;
    uint64_t checksum;
    uint64_t reserved1;
    char title[20];
    char reserved2[7];
    uint32_t gamecode;
    uint8_t rom_version;
} rom_header_t;

_Static_assert(sizeof(rom_header_t) == 64, "invalid sizeof(rom_header_t)");

__attribute__((section(".header"), used))
const rom_header_t header = {
    // Standard PI DOM1 config
    .pi_dom1_config = 0x80371240,
    // Our IPL3 does not use directly this field. We do set it
    // mainly for iQue, so that the special iQue trampoline is run,
    // which jumps to our IPL3.
    .boot_address = 0x80000400,
    // Default title name
    .title = "Libdragon           ",
};
