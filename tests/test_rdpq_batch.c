
// Test that a block generated via batch mode does not contain any
// rdpq fixup command, that would force the RSP to switch to the rdpq
// ucode. The main goal of the batch mode is in fact that of not
// requiring a context switch as much as possible.
static void assert_block_no_fixups(TestContext *ctx, rspq_block_t *block) {
    uint32_t *cmds = block->cmds;
    while (1) {
        switch (cmds[0] >> 24) {
            case 0x04: // RSPQCmd_Ret
                return;
            case 0x09: // RSPQCmd_RdpSetBuffer
                cmds += 3;
                break;
            case 0x0A: // RSPQCmd_RdpAppendBuffer
                cmds += 1;
                break;
            case 0x0B: // RSPQCmd_RdpResetMode
                cmds += 9;
                break;
            default:
                ASSERT(false, "unexpected RSP command in block: %08lx", cmds[0]);
                return;
        }

        if (cmds - block->cmds >= RSPQ_BLOCK_MIN_SIZE) {
            ASSERT(false,"block seems too large");
            return;
        }
    }
}

/*
 * Tests for frozen blocks mode.
 *
 * The goal of these tests is to verify that rdpq_mode works correctly in frozen 
 * mode. Frozen blocks mode requirers a C implementation of the RSP RDPQ_UpdateRenderMode,
 * and we need to verify that the C implementation is identical to the RSP one.
 *
 * So what we do here is to run the same sequence of rdpq_mode commands with
 * and without batch mode, and compare the results, in terms of RDP state. We
 * don't want to check that the rdpq_mode does something "useful": that's what
 * rdpq_mode tests are for. We just want to verify that the C implementation of
 * the RSP RDPQ_UpdateRenderMode is identical to the RSP one.
 */
static void run_batch_test(TestContext *ctx, void (*batch_fn)(void)) {
    // Enable frozen blocks mode to avoid RSP code execution for the batch
    rdpq_config_enable(RDPQ_CFG_FROZEN_BLOCKS);

    // Run the batch function and get the reference values
    rdpq_set_mode_standard();
    batch_fn();
    uint64_t ref_som = rdpq_get_other_modes_raw();
    uint64_t ref_cc = rdpq_get_combiner_raw() & ~0x7F00000000000000ull;

    // Now run it within a batch and compare the results
    rdpq_set_mode_standard();
    rspq_wait();
    debug_rdp_stream_reset();
    rdpq_mode_begin();
        batch_fn();
    rdpq_mode_end();
    uint64_t test_som = rdpq_get_other_modes_raw();
    uint64_t test_cc = rdpq_get_combiner_raw() & ~0x7F00000000000000ull;
    ASSERT_EQUAL_HEX(test_som, ref_som, "batch: SOM mismatch");
    ASSERT_EQUAL_HEX(test_cc, ref_cc, "batch: combiner mismatch");

    int num_ccs = debug_rdp_stream_count_cmd(RDPQ_CMD_SET_COMBINE_MODE_RAW + 0xC0);
    int num_soms = debug_rdp_stream_count_cmd(RDPQ_CMD_SET_OTHER_MODES_RAW + 0xC0);
    ASSERT_EQUAL_SIGNED(num_ccs, 1, "batch: too many SET_COMBINE_MODE");
    ASSERT_EQUAL_SIGNED(num_soms, 1, "batch: too many SET_OTHER_MODES");

    // Now do the same within a block
    rdpq_set_mode_standard();
    rspq_wait();
    debug_rdp_stream_reset();
    rspq_block_begin();
        rdpq_mode_begin();
            batch_fn();
        rdpq_mode_end();
    rspq_block_t *block = rspq_block_end();
    DEFER(rspq_block_free(block));
    rdpq_debug_log_msg("block begin");
    rspq_block_run(block);
    rspq_wait();
    test_som = rdpq_get_other_modes_raw();
    test_cc = rdpq_get_combiner_raw() & ~0x7F00000000000000ull;
    ASSERT_EQUAL_HEX(test_som, ref_som, "batch: SOM mismatch");
    ASSERT_EQUAL_HEX(test_cc, ref_cc, "batch: combiner mismatch");

    num_ccs = debug_rdp_stream_count_cmd(RDPQ_CMD_SET_COMBINE_MODE_RAW + 0xC0);
    num_soms = debug_rdp_stream_count_cmd(RDPQ_CMD_SET_OTHER_MODES_RAW + 0xC0);
    ASSERT_EQUAL_SIGNED(num_ccs, 1, "batch: too many SET_COMBINE_MODE with block");
    ASSERT_EQUAL_SIGNED(num_soms, 1, "batch: too many SET_OTHER_MODES with block");

    assert_block_no_fixups(ctx, block);
    if (ctx->result == TEST_FAILED) return;
}

void test_rdpq_batch_tex_shade_full(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_mipmap(MIPMAP_INTERPOLATE, 2);
        rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, 0, BLEND_RGB, 1)));
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_tex_shade(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_mipmap(MIPMAP_INTERPOLATE, 2);
        rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, 0, BLEND_RGB, 1)));
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_combiner_2pass(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_combiner(RDPQ_COMBINER2(
            (ZERO, ZERO, ZERO, ENV), (ENV, ZERO, TEX0, PRIM),
            (TEX1, ZERO, COMBINED_ALPHA, ZERO), (ZERO, ZERO, ZERO, ZERO)));
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_fog_tex_shade(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        rdpq_mode_fog(RDPQ_FOG_STANDARD);
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_fog_shade(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_fog(RDPQ_FOG_STANDARD);
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_blender_2pass(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_blender(RDPQ_BLENDER2(
            (IN_RGB, IN_ALPHA, BLEND_RGB, INV_MUX_ALPHA),
            (CYCLE1_RGB, IN_ALPHA, MEMORY_RGB, MEMORY_CVG)));
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_aa_default(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_blender(0);
        rdpq_mode_antialias(AA_STANDARD);
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_aa_reduced(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_blender(0);
        rdpq_mode_antialias(AA_REDUCED);
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_aa_alphacompare(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_blender(0);
        rdpq_mode_antialias(AA_STANDARD);
        rdpq_mode_alphacompare(1);
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_aa_bkg_blend(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_antialias(AA_STANDARD);
        rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_mipmap_interpolate_shq(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        rdpq_mode_mipmap(MIPMAP_INTERPOLATE_SHQ, 2);
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_mipmap_nearest(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        rdpq_mode_mipmap(MIPMAP_NEAREST, 2);
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_fog_tex_flat(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_fog(RDPQ_FOG_STANDARD);
    }

    run_batch_test(ctx, batch);
}

void test_rdpq_batch_bkg_blend_no_aa(TestContext *ctx) {
    RDPQ_INIT();
    debug_rdp_stream_init();

    void batch(void) {
        rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
    }

    run_batch_test(ctx, batch);
}
