/**
 * @file rdpq_mode.c
 * @author Giovanni Bajo <giovannibajo@gmail.com>
 * @brief RDP Command queue: mode setting
 * @ingroup rdpq
 */

#include "rdpq_mode.h"
#include "rspq.h"
#include "rdpq_internal.h"
#include "yuv.h"
#include <stdint.h>
#include <string.h>

#define RDPQ_SOM_TRACK_MASK ( \
    SOMX_FOG | SOMX_LOD_INTERP_MASK | SOM_TEXTURE_LOD | \
    SOM_AA_ENABLE | SOMX_AA_REDUCED | SOM_ALPHACOMPARE_MASK | \
    SOM_ZMODE_MASK | SOM_SAMPLE_MASK | SOM_TF_MASK | \
    SOM_BLENDING | SOMX_BLEND_2PASS | SOM_CYCLE_MASK \
)

rdpq_mode_state_t rdpq_mode_state_global;
rdpq_mode_state_t rdpq_mode_state_block_diff;
rdpq_mode_state_t rdpq_mode_state_batch_diff;
rdpq_mode_state_t *rdpq_mode_state_cur = &rdpq_mode_state_global;

rdpq_mode_batch_state_t rdpq_mode_batch_state = RDPQ_BATCH_NONE;

#define RDPQ_COMB_LOD_INTERP    RDPQ_COMBINER2((TEX1, TEX0, LOD_FRAC, TEX0), (TEX1, TEX0, LOD_FRAC, TEX0), (0,0,0,0), (0,0,0,0))
#define RDPQ_COMB_LOD_SHQ       RDPQ_COMBINER2((TEX0, TEX1, K5,       0   ), (0,    0,    0,        TEX1), (0,0,0,0), (0,0,0,0))
#define RDPQ_COMB_SHADE_FOG     RDPQ_COMBINER1((0,0,0,SHADE),      (0,0,0,1))
#define RDPQ_COMB_TEX_SHADE_FOG RDPQ_COMBINER1((TEX0,0,SHADE,0),   (0,0,0,TEX0))

static const uint32_t rdpq_aa_blend_mask =
    SOM_COVERAGE_DEST_MASK | SOM_BLEND_MASK | SOM_BLALPHA_MASK | SOM_COLOR_ON_CVG_OVERFLOW;

static const uint32_t rdpq_aa_blend_table[4] = {
    SOM_COVERAGE_DEST_ZAP,                                            // AA=0 / BLEND=0
    SOM_COVERAGE_DEST_ZAP,                                            // AA=0 / BLEND=1
    SOM_BLALPHA_CVG | SOM_COVERAGE_DEST_CLAMP,                        // AA=1 / BLEND=0
    SOM_COLOR_ON_CVG_OVERFLOW | SOM_COVERAGE_DEST_WRAP,               // AA=1 / BLEND=1
};

static const uint32_t rdpq_aa_blend_default_formula[2] = {
    RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, MEMORY_CVG)),                      // Standard AA
    RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, MEMORY_CVG)) & ~SOM_READ_ENABLE,   // Reduced AA
};

static const uint64_t rdpq_combiner_mipmaps[2] = {
    (RDPQ_COMB_LOD_INTERP & RDPQ_COMB0_MASK) | RDPQ_COMBINER_2PASS,
    (RDPQ_COMB_LOD_SHQ    & RDPQ_COMB0_MASK) | RDPQ_COMBINER_2PASS,
};

void __rdpq_mode_state_merge(rdpq_mode_state_t *dst, const rdpq_mode_state_t *diff)
{
    dst->som_known_mask |= diff->som_known_mask;
    dst->som_value_mask = (dst->som_value_mask & ~diff->som_known_mask) |
                          (diff->som_value_mask & diff->som_known_mask);

    if (diff->known_mask & RDPQ_STATE_KNOWN_COMBINER)
        dst->combiner = diff->combiner;

    if (diff->known_mask & RDPQ_STATE_KNOWN_BLEND_STEP0)
        dst->blend_step0 = diff->blend_step0;
    if (diff->known_mask & RDPQ_STATE_KNOWN_BLEND_STEP1)
        dst->blend_step1 = diff->blend_step1;
    dst->known_mask |= diff->known_mask;
}

void __rdpq_mode_state_apply_som(rdpq_mode_state_t *state, uint64_t mask, uint64_t val)
{
    state->som_known_mask |= mask;
    state->som_value_mask = (state->som_value_mask & ~mask) | (val & mask);
}

static void rdpq_mode_state_apply_cmd(uint32_t cmd_id, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
    rdpq_mode_state_t *state = rdpq_mode_state_cur;

    switch (cmd_id) {
    case RDPQ_CMD_SET_BLENDING_MODE: {
        uint32_t step0 = state->blend_step0;
        if (w1 != 0) {
            state->blend_step1 = w1;
            state->known_mask |= RDPQ_STATE_KNOWN_BLEND_STEP1;
            step0 = w1;
        }
        if (step0 & SOMX_BLEND_2PASS) {
            state->blend_step0 = w1;
            state->known_mask |= RDPQ_STATE_KNOWN_BLEND_STEP0;
        }
        break;
    }
    case RDPQ_CMD_SET_FOG_MODE:
        state->blend_step0 = w1;
        state->known_mask |= RDPQ_STATE_KNOWN_BLEND_STEP0;
        break;
    case RDPQ_CMD_SET_COMBINE_MODE_1PASS:
        state->combiner = ((uint64_t)w0 << 32) | w1;
        state->known_mask |= RDPQ_STATE_KNOWN_COMBINER;
        break;
    case RDPQ_CMD_SET_COMBINE_MODE_2PASS:
        state->combiner = ((uint64_t)w0 << 32) | w1 | RDPQ_COMBINER_2PASS;
        state->known_mask |= RDPQ_STATE_KNOWN_COMBINER;
        break;
    case RDPQ_CMD_MODIFY_OTHER_MODES: {
        uint32_t offset = w0 & 0xF;
        uint64_t mask = 0;
        uint64_t val = 0;
        if (offset == 0) {
            mask = ((uint64_t)(~w1) << 32);
            val = ((uint64_t)w2 << 32);
        } else {
            mask = (uint64_t)(~w1) & 0xFFFFFFFFu;
            val = (uint64_t)w2 & 0xFFFFFFFFu;
        }
        __rdpq_mode_state_apply_som(state, mask, val);
        break;
    }
    default:
        assertf(false, "unexpected mode cmd_id=%lu", (unsigned long)cmd_id);
        break;
    }
}

static uint64_t rdpq_shadow_combiner_mask(uint64_t combiner)
{
    uint64_t comb1_mask = RDPQ_COMB1_MASK;
    if (((combiner >> 0 ) &  7) == 1) comb1_mask ^= 1ull << 0;
    if (((combiner >> 3 ) &  7) == 1) comb1_mask ^= 1ull << 3;
    if (((combiner >> 6 ) &  7) == 1) comb1_mask ^= 1ull << 6;
    if (((combiner >> 18) &  7) == 1) comb1_mask ^= 1ull << 18;
    if (((combiner >> 21) &  7) == 1) comb1_mask ^= 1ull << 21;
    if (((combiner >> 24) &  7) == 1) comb1_mask ^= 1ull << 24;
    if (((combiner >> 32) & 31) == 1) comb1_mask ^= 1ull << 32;
    if (((combiner >> 37) & 15) == 1) comb1_mask ^= 1ull << 37;
    return comb1_mask;
}

static void rdpq_update_render_mode_cpu(const rdpq_mode_state_t *state,
                                        uint64_t *comb_rdp_out, uint64_t *som_rdp_out)
{
    uint64_t comb_1cyc = 0;
    uint64_t comb_2cyc = 0;
    uint32_t som_hi = (uint32_t)(state->som_value_mask >> 32);
    uint32_t som_lo = (uint32_t)state->som_value_mask;

#if 0
    // copy/fill mode: for now we don't handle this case
    if (som_hi & (1u << (SOM_CYCLE_SHIFT - 32 + 1))) {
        uint32_t final_hi = (som_hi | 0xFF000000) ^ 0x10000000;
        *som_out = ((uint64_t)final_hi << 32) | som_lo;
        return;
    }
#endif

    uint32_t comb_hi = (uint32_t)(state->combiner >> 32);
    uint32_t comb_lo = (uint32_t)state->combiner;

    if (comb_hi & 0x80000000) {
        if (som_hi & (uint32_t)(SOMX_LOD_INTERPOLATE >> 32))
            assertf(false, "RDPQ: interpolated mipmaps require 1-pass combiner");
        comb_2cyc = state->combiner;
        comb_1cyc = comb_2cyc;
    } else {
        if (som_hi & (uint32_t)(SOMX_FOG >> 32)) {
            uint32_t comb_hi_noid = comb_hi & 0x00FFFFFF;
            uint32_t tex_shade_hi = (uint32_t)(RDPQ_COMBINER_TEX_SHADE >> 32) & 0x00FFFFFF;
            uint32_t shade_hi = (uint32_t)(RDPQ_COMBINER_SHADE >> 32) & 0x00FFFFFF;
            if (comb_hi_noid == tex_shade_hi && comb_lo == (uint32_t)RDPQ_COMBINER_TEX_SHADE) {
                comb_hi = (uint32_t)(RDPQ_COMB_TEX_SHADE_FOG >> 32);
                comb_lo = (uint32_t)RDPQ_COMB_TEX_SHADE_FOG;
            } else if (comb_hi_noid == shade_hi && comb_lo == (uint32_t)RDPQ_COMBINER_SHADE) {
                comb_hi = (uint32_t)(RDPQ_COMB_SHADE_FOG >> 32);
                comb_lo = (uint32_t)RDPQ_COMB_SHADE_FOG;
            }
        }

        uint32_t interp = (som_hi & (uint32_t)(SOMX_LOD_INTERP_MASK >> 32)) >> (SOMX_LOD_INTERP_SHIFT - 32);
        if (interp) {
            uint64_t mask = rdpq_shadow_combiner_mask(state->combiner);
            uint64_t comb_masked = ((uint64_t)(comb_hi & (uint32_t)(mask >> 32)) << 32) |
                                   (uint32_t)(comb_lo & (uint32_t)mask);
            uint64_t comb_mipmap = rdpq_combiner_mipmaps[interp - 1];
            comb_2cyc = comb_masked | comb_mipmap;
        } else {
            comb_1cyc = ((uint64_t)comb_hi << 32) | comb_lo;
            uint64_t comb_passthrough = ((uint64_t)(comb_hi & (uint32_t)(RDPQ_COMB0_MASK >> 32)) << 32) |
                                        (uint32_t)(comb_lo & (uint32_t)RDPQ_COMB0_MASK);
            if (som_hi & (uint32_t)(SOM_TEXTURE_LOD >> 32))
                comb_passthrough |= RDPQ_COMBINER_2PASS;
            comb_2cyc = comb_passthrough;
        }
    }

    uint32_t blend0 = state->blend_step0;
    uint32_t blend1 = state->blend_step1;
    bool bkg_blending = blend1 != 0;

    if (!blend1 && (som_lo & SOM_AA_ENABLE)) {
        uint32_t reduced = (som_hi & (uint32_t)(SOMX_AA_REDUCED >> 32)) ? 1 : 0;
        blend1 = rdpq_aa_blend_default_formula[reduced];
        blend0 &= ~SOM_BLENDING;
    }

    uint32_t blend_1cyc = 0;
    uint32_t blend_2cyc = 0;
    if (!blend0) {
        blend_1cyc = blend1;
    } else if (!blend1) {
        blend_1cyc = blend0;
    } else {
        blend_2cyc = (blend0 & (uint32_t)SOM_BLEND0_MASK) | (blend1 & (uint32_t)SOM_BLEND1_MASK) | SOMX_BLEND_2PASS;
    }
    if (!blend_2cyc) {
        blend_2cyc = blend_1cyc & (uint32_t)SOM_BLEND1_MASK;
        blend_1cyc &= (uint32_t)SOM_BLEND0_MASK;
    }

    uint32_t tf_sample = som_hi & (uint32_t)((SOM_TF_MASK | SOM_SAMPLE_MASK) >> 32);
    uint32_t tf_yuv = (uint32_t)((SOM_SAMPLE_BILINEAR | SOM_TF0_YUV | SOM_TF1_YUV) >> 32);
    if (tf_sample == tf_yuv) {
        som_hi |= (uint32_t)((SOM_TF1_YUVTEX0 | SOM_TF0_RGB) >> 32);
    }

    bool need_2cyc = (blend_2cyc & SOMX_BLEND_2PASS) || (comb_2cyc & RDPQ_COMBINER_2PASS);
    uint64_t comb_final = need_2cyc ? comb_2cyc : comb_1cyc;
    uint32_t blend_final = need_2cyc ? blend_2cyc : blend_1cyc;

    som_hi = (som_hi & ~((SOM_CYCLE_MASK >> 32) | 0xFF000000));
    if (need_2cyc)
        som_hi |= (SOM_CYCLE_2 >> 32);
    else
        som_hi |= (SOM_CYCLE_1 >> 32);

    int aa_index = (som_lo & SOM_AA_ENABLE ? 2 : 0) + (bkg_blending ? 1 : 0);
    uint32_t aa_bits = rdpq_aa_blend_table[aa_index];
    uint32_t blend_bits = (aa_bits | blend_final) & rdpq_aa_blend_mask;
    som_lo = (som_lo & ~rdpq_aa_blend_mask) | blend_bits;

    uint32_t aa_alpha_mask = SOM_ALPHACOMPARE_THRESHOLD | SOM_BLALPHA_CVG;
    if ((som_lo & aa_alpha_mask) == aa_alpha_mask) {
        som_lo |= SOM_BLALPHA_CVG_TIMES_CC;
        som_lo &= ~SOM_ALPHACOMPARE_MASK;
    }

    if ((som_lo & (1u << 10)) == 0) {
        if ((som_lo & SOM_BLENDING) == 0)
            som_lo |= 2u << 10;
        som_lo ^= 2u << 10;
    }

    *comb_rdp_out = comb_final & 0x00FFFFFFFFFFFFFFull;
    *som_rdp_out = ((uint64_t)som_hi << 32) | som_lo;
}

/** 
 * @brief Like #rdpq_write, but for mode commands.
 * 
 * During CPU-side batching (#rdpq_mode_begin), mode commands are handled
 * separately and not emitted via RSP, so we can avoid reserving space in
 * the RDP static buffer in blocks.
 */
#define rdpq_mode_write(num_rdp_commands, num_batched_rdp_commands, ...) ({ \
    rdpq_write(\
        rdpq_mode_batch_state != RDPQ_BATCH_NONE ? \
            num_batched_rdp_commands : num_rdp_commands, \
        ##__VA_ARGS__); \
})

/** 
 * @brief Write a fixup that changes the current render mode (8-byte command)
 * 
 * All the mode fixups always need to update the RDP render mode
 * and thus generate two RDP commands: SET_COMBINE and SET_OTHER_MODES.
 */
__attribute__((noinline))
void __rdpq_fixup_mode(uint32_t cmd_id, uint32_t w0, uint32_t w1)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    rdpq_mode_state_apply_cmd(cmd_id, w0, w1, 0, 0);
    if (rdpq_mode_batch_state == RDPQ_BATCH_DEFERRED)
        return;
    rdpq_mode_write(2, 0, RDPQ_OVL_ID, cmd_id, w0, w1);  // COMBINE+SOM
}

/** @brief Write a fixup that changes the current render mode (12-byte command) */
__attribute__((noinline))
void __rdpq_fixup_mode3(uint32_t cmd_id, uint32_t w0, uint32_t w1, uint32_t w2)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    rdpq_mode_state_apply_cmd(cmd_id, w0, w1, w2, 0);
    if (rdpq_mode_batch_state == RDPQ_BATCH_DEFERRED)
        return;
    rdpq_mode_write(2, 0, RDPQ_OVL_ID, cmd_id, w0, w1, w2);  // COMBINE+SOM
}

/** @brief Write a fixup that changes the current render mode (16-byte command) */
__attribute__((noinline))
void __rdpq_fixup_mode4(uint32_t cmd_id, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    rdpq_mode_state_apply_cmd(cmd_id, w0, w1, w2, w3);
    if (rdpq_mode_batch_state == RDPQ_BATCH_DEFERRED)
        return;
    rdpq_mode_write(2, 0, RDPQ_OVL_ID, cmd_id, w0, w1, w2, w3);  // COMBINE+SOM
}

/** @brief Write a fixup to reset the render mode */
__attribute__((noinline))
void __rdpq_reset_render_mode(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    assertf(rdpq_mode_batch_state != RDPQ_BATCH_DEFERRED,
        "reset render mode while in deferred batch");
    // ResetRenderMode can generate: SCISSOR+COMBINE+SOM when not frozen,
    // or just SCISSOR when frozen.
    rdpq_mode_write(3, 1, RDPQ_OVL_ID, RDPQ_CMD_RESET_RENDER_MODE, w0, w1, w2, w3);
}

void rdpq_mode_push(void)
{
    assertf(rdpq_mode_batch_state == RDPQ_BATCH_NONE,
        "rdpq_mode_push not allowed inside rdpq_mode_begin/end");
    // Push is not a RDP passthrough/fixup command, it's just a standard
    // RSP command. Use rspq_write.
    rspq_write(RDPQ_OVL_ID, RDPQ_CMD_PUSH_RENDER_MODE);
}

void rdpq_mode_pop(void)
{
    assertf(rdpq_mode_batch_state == RDPQ_BATCH_NONE,
        "rdpq_mode_pop not allowed inside rdpq_mode_begin/end");
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    // ModePop can generate: SCISSOR+COMBINE+SOM when not frozen,
    // or just SCISSOR when frozen.
    rdpq_mode_write(3, 1, RDPQ_OVL_ID, RDPQ_CMD_POP_RENDER_MODE);
    // When recording a block, the popped state is unknown: force fixups.
    if (rspq_block_is_recording())
        *rdpq_mode_state_cur = (rdpq_mode_state_t){0};
}

/** @brief Like #rdpq_set_mode_fill, but without fill color configuration */
void __rdpq_set_mode_fill(void) {
    uint64_t som = (0xEFull << 56) | SOM_CYCLE_FILL;
    *rdpq_mode_state_cur = (rdpq_mode_state_t){
        .som_known_mask = UINT64_MAX,
        .som_value_mask = som,
        .known_mask = RDPQ_STATE_KNOWN_ALL,
    };
    if (rdpq_mode_batch_state != RDPQ_BATCH_DEFERRED)
        __rdpq_reset_render_mode(0, 0, som >> 32, som & 0xFFFFFFFF);
}

void rdpq_set_mode_copy(bool transparency) {
    uint64_t som = (0xEFull << 56) | SOM_CYCLE_COPY | (transparency ? SOM_ALPHACOMPARE_THRESHOLD : 0);
    *rdpq_mode_state_cur = (rdpq_mode_state_t){
        .som_known_mask = UINT64_MAX,
        .som_value_mask = som,
        .known_mask = RDPQ_STATE_KNOWN_ALL,
    };
    if (rdpq_mode_batch_state != RDPQ_BATCH_DEFERRED)
        __rdpq_reset_render_mode(0, 0, som >> 32, som & 0xFFFFFFFF);
}

void rdpq_set_mode_standard(void) {
    uint64_t cc = RDPQ_COMBINER1(
        (ZERO, ZERO, ZERO, TEX0), (ZERO, ZERO, ZERO, TEX0)
    );
    uint64_t som =
        SOM_TF0_RGB | SOM_TF1_RGB |
        SOM_RGBDITHER_NONE | SOM_ALPHADITHER_NONE |
        SOM_COVERAGE_DEST_ZAP;

    *rdpq_mode_state_cur = (rdpq_mode_state_t){
        .som_known_mask = UINT64_MAX,
        .som_value_mask = som,
        .combiner = cc,
        .known_mask = RDPQ_STATE_KNOWN_ALL,
    };
    if (rdpq_mode_batch_state != RDPQ_BATCH_DEFERRED) {
        __rdpq_reset_render_mode(
            cc >> 32,   cc & 0xFFFFFFFF,
            som >> 32, som & 0xFFFFFFFF);
    }
    rdpq_mode_combiner(cc);
}

void rdpq_set_mode_yuv(bool bilinear) {
    uint64_t cc, som;

    som = SOM_RGBDITHER_NONE | SOM_ALPHADITHER_NONE | SOM_TF0_YUV | SOM_TF1_YUV;
    cc = RDPQ_COMBINER1((TEX0, K4, K5, TEX0), (0, 0, 0, 1));

    *rdpq_mode_state_cur = (rdpq_mode_state_t){
        .som_known_mask = UINT64_MAX,
        .som_value_mask = som,
        .combiner = cc,
        .known_mask = RDPQ_STATE_KNOWN_ALL,
    };
    if (rdpq_mode_batch_state != RDPQ_BATCH_DEFERRED) {
        __rdpq_reset_render_mode(
            cc >> 32,   cc & 0xFFFFFFFF,
            som >> 32, som & 0xFFFFFFFF);
    }
    if (bilinear) {
        rdpq_mode_filter(FILTER_BILINEAR);
        cc = RDPQ_COMBINER1((TEX1, K4, K5, TEX1), (0, 0, 0, 1));
    }
    rdpq_mode_combiner(cc);

    // Set the YUV coefficients (FIXME: make this configurable)
    const yuv_colorspace_t *cs = &YUV_BT601_TV;
    rdpq_set_yuv_parms(cs->k0, cs->k1, cs->k2, cs->k3, cs->k4, cs->k5);
}

void rdpq_mode_begin(void)
{
    assertf(rdpq_mode_batch_state == RDPQ_BATCH_NONE, "rdpq_mode_begin nested calls not supported");
    if (__rdpq_config & RDPQ_CFG_FROZEN_BLOCKS) {
        rdpq_mode_batch_state = RDPQ_BATCH_DEFERRED;
        rdpq_mode_state_batch_diff = (rdpq_mode_state_t){0};
        rdpq_mode_state_cur = &rdpq_mode_state_batch_diff;
    } else {
        rdpq_mode_batch_state = RDPQ_BATCH_PENDING;
        __rdpq_mode_change_som(SOMX_UPDATE_FREEZE, SOMX_UPDATE_FREEZE);
    }
}

void rdpq_mode_end(void)
{
    assertf(rdpq_mode_batch_state != RDPQ_BATCH_NONE, "rdpq_mode_end called without begin");
    if (rdpq_mode_batch_state == RDPQ_BATCH_PENDING) {
        __rdpq_mode_change_som(SOMX_UPDATE_FREEZE, 0);
        rdpq_mode_batch_state = RDPQ_BATCH_NONE;
        return;
    }
    rdpq_mode_state_t merged = rdpq_mode_state_global;
    if (rspq_block_is_recording())
        __rdpq_mode_state_merge(&merged, &rdpq_mode_state_block_diff);
    __rdpq_mode_state_merge(&merged, &rdpq_mode_state_batch_diff);

    assertf((merged.som_known_mask & RDPQ_SOM_TRACK_MASK) == RDPQ_SOM_TRACK_MASK,
        "rdpq_mode_end: unknown SOM bits in tracked mask");
    assertf((merged.known_mask & RDPQ_STATE_KNOWN_COMBINER), "rdpq_mode_end: combiner unknown");
    assertf((merged.known_mask & RDPQ_STATE_KNOWN_BLEND_MASK) == RDPQ_STATE_KNOWN_BLEND_MASK,
        "rdpq_mode_end: blender steps unknown");

    uint64_t comb_final = 0;
    uint64_t som_final = 0;
    rdpq_update_render_mode_cpu(&merged, &comb_final, &som_final);

    rdpq_passthrough_write((RDPQ_CMD_SET_OTHER_MODES_RAW, som_final >> 32, som_final & 0xFFFFFFFF));
    rdpq_passthrough_write((RDPQ_CMD_SET_COMBINE_MODE_RAW, comb_final >> 32, comb_final & 0xFFFFFFFF));
    uint64_t comb_mask = rdpq_shadow_combiner_mask(merged.combiner);
    uint64_t som_sync = (som_final & 0x00FFFFFFFFFFFFFFull) |
                        (merged.som_value_mask & 0xFF00000000000000ull);
    rspq_int_write(RSPQ_CMD_RDP_RESET_MODE, 0,
        merged.combiner >> 32, merged.combiner & 0xFFFFFFFF,
        comb_mask >> 32, comb_mask & 0xFFFFFFFF,
        merged.blend_step0, merged.blend_step1,
        som_sync >> 32, som_sync & 0xFFFFFFFF);

    if (rspq_block_is_recording())
        __rdpq_mode_state_merge(&rdpq_mode_state_block_diff, &rdpq_mode_state_batch_diff);
    else
        __rdpq_mode_state_merge(&rdpq_mode_state_global, &rdpq_mode_state_batch_diff);

    rdpq_mode_batch_state = RDPQ_BATCH_NONE;
    rdpq_mode_state_cur = rspq_block_is_recording() ? &rdpq_mode_state_block_diff : &rdpq_mode_state_global;
}


/* Extern inline instantiations. */
extern inline void rdpq_set_mode_fill(color_t color);
extern inline void rdpq_set_mode_standard(void);
extern inline void rdpq_mode_combiner(rdpq_combiner_t comb);
extern inline void rdpq_mode_blender(rdpq_blender_t blend);
extern inline void rdpq_mode_antialias(rdpq_antialias_t mode);
extern inline void rdpq_mode_fog(rdpq_blender_t fog);
extern inline void rdpq_mode_dithering(rdpq_dither_t dither);
extern inline void rdpq_mode_alphacompare(int threshold);
extern inline void rdpq_mode_zbuf(bool compare, bool write);
extern inline void rdpq_mode_zoverride(bool enable, float z, int16_t deltaz);
extern inline void rdpq_mode_tlut(rdpq_tlut_t tlut);
extern inline void rdpq_mode_filter(rdpq_filter_t s);
extern inline void rdpq_mode_mipmap(rdpq_mipmap_t mode, int num_levels);
extern inline void rdpq_mode_persp(bool perspective);
///@cond
extern inline void __rdpq_mode_change_som(uint64_t mask, uint64_t val);
///@endcond
