
#include <malloc.h>
#include "../src/ugfx/ugfx_internal.h"

static volatile int dp_intr_raised;

const unsigned long ugfx_timeout = 100;

void dp_interrupt_handler()
{
    dp_intr_raised = 1;
}

void wait_for_dp_interrupt(unsigned long timeout)
{
    unsigned long time_start = get_ticks_ms();

    while (get_ticks_ms() - time_start < timeout) {
        // Wait until the interrupt was raised
        if (dp_intr_raised) {
            break;
        }
    }
}

void test_ugfx_rdp_interrupt(TestContext *ctx)
{
    dp_intr_raised = 0;
    register_DP_handler(dp_interrupt_handler);
    DEFER(unregister_DP_handler(dp_interrupt_handler));
    set_DP_interrupt(1);
    DEFER(set_DP_interrupt(0));

    dl_init();
    DEFER(dl_close());
    ugfx_init();
    DEFER(ugfx_close());

    dl_start();
    rdp_sync_full();

    wait_for_dp_interrupt(ugfx_timeout);

    ASSERT(dp_intr_raised, "Interrupt was not raised!");
}

void test_ugfx_dram_buffer(TestContext *ctx)
{
    dp_intr_raised = 0;
    register_DP_handler(dp_interrupt_handler);
    DEFER(unregister_DP_handler(dp_interrupt_handler));
    set_DP_interrupt(1);
    DEFER(set_DP_interrupt(0));

    dl_init();
    DEFER(dl_close());
    ugfx_init();
    DEFER(ugfx_close());

    extern uint8_t __ugfx_dram_buffer[];
    data_cache_hit_writeback_invalidate(__ugfx_dram_buffer, UGFX_RDP_DRAM_BUFFER_SIZE);

    dl_start();

    const uint32_t fbsize = 32 * 32 * 2;
    void *framebuffer = memalign(64, fbsize);
    DEFER(free(framebuffer));
    memset(framebuffer, 0, fbsize);

    data_cache_hit_writeback_invalidate(framebuffer, fbsize);

    rdp_set_other_modes(SOM_CYCLE_FILL);
    rdp_set_scissor(0, 0, 32 << 2, 32 << 2);
    rdp_set_fill_color(0xFFFFFFFF);
    dl_noop();
    rdp_set_color_image((uint32_t)framebuffer, RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, 32);
    rdp_fill_rectangle(0, 0, 32 << 2, 32 << 2);
    rdp_sync_full();

    wait_for_dp_interrupt(ugfx_timeout);

    ASSERT(dp_intr_raised, "Interrupt was not raised!");

    uint64_t expected_data[] = {
        RdpSetOtherModes(SOM_CYCLE_FILL),
        RdpSetClippingFX(0, 0, 32 << 2, 32 << 2),
        RdpSetFillColor(0xFFFFFFFF),
        RdpSetColorImage(RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, 32, (uint32_t)framebuffer),
        RdpFillRectangleFX(0, 0, 32 << 2, 32 << 2),
        RdpSyncFull()
    };

    ASSERT_EQUAL_MEM(UncachedAddr(__ugfx_dram_buffer), (uint8_t*)expected_data, sizeof(expected_data), "Unexpected data in DRAM buffer!");

    for (uint32_t i = 0; i < 32 * 32; i++)
    {
        ASSERT_EQUAL_HEX(UncachedUShortAddr(framebuffer)[i], 0xFFFF, "Framebuffer was not cleared properly! Index: %lu", i);
    }
}

void test_ugfx_fill_dmem_buffer(TestContext *ctx)
{
    dp_intr_raised = 0;
    register_DP_handler(dp_interrupt_handler);
    DEFER(unregister_DP_handler(dp_interrupt_handler));
    set_DP_interrupt(1);
    DEFER(set_DP_interrupt(0));

    dl_init();
    DEFER(dl_close());
    ugfx_init();
    DEFER(ugfx_close());

    dl_start();

    const uint32_t fbsize = 32 * 32 * 2;
    void *framebuffer = memalign(64, fbsize);
    DEFER(free(framebuffer));
    memset(framebuffer, 0, fbsize);

    data_cache_hit_writeback_invalidate(framebuffer, fbsize);

    rdp_set_other_modes(SOM_CYCLE_FILL);
    rdp_set_scissor(0, 0, 32 << 2, 32 << 2);
    rdp_set_fill_color(0xFFFFFFFF);

    for (uint32_t i = 0; i < UGFX_RDP_DMEM_BUFFER_SIZE / 8; i++)
    {
        rdp_set_prim_color(0x0);
    }

    rdp_set_color_image((uint32_t)framebuffer, RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, 32);
    rdp_fill_rectangle(0, 0, 32 << 2, 32 << 2);
    rdp_sync_full();

    wait_for_dp_interrupt(ugfx_timeout);

    ASSERT(dp_intr_raised, "Interrupt was not raised!");
    
    for (uint32_t i = 0; i < 32 * 32; i++)
    {
        ASSERT_EQUAL_HEX(UncachedUShortAddr(framebuffer)[i], 0xFFFF, "Framebuffer was not cleared properly! Index: %lu", i);
    }
}

void test_ugfx_fill_dram_buffer(TestContext *ctx)
{
    dp_intr_raised = 0;
    register_DP_handler(dp_interrupt_handler);
    DEFER(unregister_DP_handler(dp_interrupt_handler));
    set_DP_interrupt(1);
    DEFER(set_DP_interrupt(0));

    dl_init();
    DEFER(dl_close());
    ugfx_init();
    DEFER(ugfx_close());

    dl_start();

    const uint32_t fbsize = 32 * 32 * 2;
    void *framebuffer = memalign(64, fbsize);
    DEFER(free(framebuffer));
    memset(framebuffer, 0, fbsize);

    data_cache_hit_writeback_invalidate(framebuffer, fbsize);

    rdp_set_other_modes(SOM_CYCLE_FILL);
    rdp_set_scissor(0, 0, 32 << 2, 32 << 2);
    rdp_set_fill_color(0xFFFFFFFF);

    for (uint32_t i = 0; i < UGFX_RDP_DRAM_BUFFER_SIZE / 8; i++)
    {
        rdp_set_prim_color(0x0);
    }

    rdp_set_color_image((uint32_t)framebuffer, RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, 32);
    rdp_fill_rectangle(0, 0, 32 << 2, 32 << 2);
    rdp_sync_full();

    wait_for_dp_interrupt(ugfx_timeout);

    ASSERT(dp_intr_raised, "Interrupt was not raised!");
    
    for (uint32_t i = 0; i < 32 * 32; i++)
    {
        ASSERT_EQUAL_HEX(UncachedUShortAddr(framebuffer)[i], 0xFFFF, "Framebuffer was not cleared properly! Index: %lu", i);
    }
}
