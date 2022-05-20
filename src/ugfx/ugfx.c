#include <libdragon.h>
#include <stdlib.h>
#include <string.h>

#include "ugfx_internal.h"

static void ugfx_assert_handler(rsp_snapshot_t *state, uint16_t assert_code);
static void ugfx_crash_handler(rsp_snapshot_t *state);

DEFINE_RSP_UCODE(rsp_ugfx, 
    .crash_handler = ugfx_crash_handler,
    .assert_handler = ugfx_assert_handler);

uint8_t __ugfx_dram_buffer[UGFX_RDP_DRAM_BUFFER_SIZE];

static bool __ugfx_initialized = 0;

void ugfx_init()
{
    if (__ugfx_initialized) {
        return;
    }

    ugfx_state_t *ugfx_state = UncachedAddr(rspq_overlay_get_state(&rsp_ugfx));

    memset(ugfx_state, 0, sizeof(ugfx_state_t));

    ugfx_state->dram_buffer = PhysicalAddr(__ugfx_dram_buffer);
    ugfx_state->dram_buffer_size = UGFX_RDP_DRAM_BUFFER_SIZE;

    rspq_init();
    rspq_overlay_register(&rsp_ugfx, 2);
    rspq_overlay_register(&rsp_ugfx, 3);

    __ugfx_initialized = 1;
}

void ugfx_close()
{
    __ugfx_initialized = 0;
}

static void ugfx_assert_handler(rsp_snapshot_t *state, uint16_t assert_code)
{
    switch (assert_code) {
    case ASSERT_RDP_FROZEN:
        printf("RDP display list stalled\n");
    }
}

static void ugfx_crash_handler(rsp_snapshot_t *state)
{
    // Dump RDP display list contents around the current pointer
    if (state->cop0[10] != 0) {    
        debugf("UGFX: RDP Display List\n");
        uint64_t *cur = (void*)(state->cop0[10] | 0xA0000000);
        for (int i=0; i<64; i++) {
            debugf("%016llx%c", cur[i-32], i==32 ? '*' : ' ');
            if (i%8 == 7) debugf("\n");
        }
    }
}

