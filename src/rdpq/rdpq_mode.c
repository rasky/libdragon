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
#include <string.h>

static rspq_rdp_mode_t rdpq_mode_shadow;
static bool rdpq_mode_cpu_track = false;

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

static uint64_t rdpq_shadow_encode_combiner(uint32_t w0, uint32_t w1)
{
    //uint32_t hi = (w0 | 0x7F000000) ^ 0x03000000;
    return ((uint64_t)w0 << 32) | w1;
}

static void rdpq_shadow_reset(uint64_t som)
{
    memset(&rdpq_mode_shadow, 0, sizeof(rdpq_mode_shadow));
    rdpq_mode_shadow.other_modes = som & 0x00FFFFFFFFFFFFFFull;
}

static void rdpq_shadow_apply_cmd(uint32_t cmd_id, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
    switch (cmd_id) {
    case RDPQ_CMD_SET_BLENDING_MODE: {
        uint32_t step0 = rdpq_mode_shadow.blend_step0;
        if (w1 != 0) {
            rdpq_mode_shadow.blend_step1 = w1;
            step0 = w1;
        }
        if (step0 & SOMX_BLEND_2PASS)
            rdpq_mode_shadow.blend_step0 = w1;
        break;
    }
    case RDPQ_CMD_SET_FOG_MODE:
        rdpq_mode_shadow.blend_step0 = w1;
        break;
    case RDPQ_CMD_SET_COMBINE_MODE_1PASS:
        rdpq_mode_shadow.combiner_mipmapmask = ((uint64_t)w2 << 32) | w3;
        rdpq_mode_shadow.combiner = ((uint64_t)w0 << 32) | w1;
        break;
    case RDPQ_CMD_SET_COMBINE_MODE_2PASS:
        rdpq_mode_shadow.combiner = ((uint64_t)w0 << 32) | w1 | RDPQ_COMBINER_2PASS;
        break;
    case RDPQ_CMD_MODIFY_OTHER_MODES: {
        debugf("[rdpq_shadow_apply_cmd] ModifyOtherModes: offset=%lx mask=%08lx value=%08lx\n", w0, w1, w2);
        uint32_t offset = w0 & 0xF;
        uint32_t som_hi = (uint32_t)(rdpq_mode_shadow.other_modes >> 32);
        uint32_t som_lo = (uint32_t)rdpq_mode_shadow.other_modes;
        if (offset == 0) {
            som_hi = (som_hi & w1) | w2;
        } else {
            som_lo = (som_lo & w1) | w2;
        }
        rdpq_mode_shadow.other_modes = ((uint64_t)som_hi << 32) | som_lo;
        break;
    }
    default:
        assertf(false, "unexpected mode cmd_id=%lu", (unsigned long)cmd_id);
        break;
    }
}

static void rdpq_update_render_mode_cpu(uint64_t *comb_rdp_out, uint64_t *som_rdp_out)
{
    uint64_t comb_1cyc = 0;
    uint64_t comb_2cyc = 0;
    uint32_t som_hi = (uint32_t)(rdpq_mode_shadow.other_modes >> 32);
    uint32_t som_lo = (uint32_t)rdpq_mode_shadow.other_modes;

#if 0
    // copy/fill mode: for now we don't handle this case
    if (som_hi & (1u << (SOM_CYCLE_SHIFT - 32 + 1))) {
        uint32_t final_hi = (som_hi | 0xFF000000) ^ 0x10000000;
        *som_out = ((uint64_t)final_hi << 32) | som_lo;
        return;
    }
#endif

    uint32_t comb_hi = (uint32_t)(rdpq_mode_shadow.combiner >> 32);
    uint32_t comb_lo = (uint32_t)rdpq_mode_shadow.combiner;

    debugf("rdpq_update_render_mode_cpu: som_hi=%08lx som_lo=%08lx\n", som_hi, som_lo);
    debugf("rdpq_update_render_mode_cpu: comb_hi=%08lx comb_lo=%08lx\n", comb_hi, comb_lo);

    if (comb_hi & 0x80000000) {
        if (som_hi & (uint32_t)(SOMX_LOD_INTERPOLATE >> 32))
            assertf(false, "RDPQ: interpolated mipmaps require 1-pass combiner");
        comb_2cyc = rdpq_mode_shadow.combiner;
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
            uint64_t mask = rdpq_mode_shadow.combiner_mipmapmask;
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

    uint32_t blend0 = rdpq_mode_shadow.blend_step0;
    uint32_t blend1 = rdpq_mode_shadow.blend_step1;
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

    // uint32_t cycle_type = need_2cyc ?
    //     ((uint32_t)((SOM_CYCLE_MASK ^ SOM_CYCLE_2) >> 32) | 0x10000000) :
    //     ((uint32_t)((SOM_CYCLE_MASK ^ SOM_CYCLE_1) >> 32) | 0x10000000);
    // som_hi = (som_hi | (uint32_t)(SOM_CYCLE_MASK >> 32) | 0xFF000000) ^ cycle_type;
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

    // uint32_t comb_final_hi = (uint32_t)(comb_final >> 32);
    // comb_final_hi = (comb_final_hi | 0xFF000000) ^ 0x03000000;
    // comb_final = ((uint64_t)comb_final_hi << 32) | (uint32_t)comb_final;

    *comb_rdp_out = comb_final & 0x00FFFFFFFFFFFFFFull;
    *som_rdp_out = ((uint64_t)som_hi << 32) | som_lo;

    // Keep MSB of other modes for the RDPQ_OTHER_MODES register
    som_hi |= rdpq_mode_shadow.other_modes & 0xFF000000;
    rdpq_mode_shadow.other_modes = ((uint64_t)som_hi << 32) | som_lo;
}

/** 
 * @brief Like #rdpq_write, but for mode commands.
 * 
 * During freeze (#rdpq_mode_begin), mode commands don't emit RDP commands
 * as they are batched instead, so we can avoid reserving space in the
 * RDP static buffer in blocks.
 */
#define rdpq_mode_write(num_rdp_commands, num_frozen_rdp_commands, ...) ({ \
    rdpq_write(rdpq_tracking.mode_freeze ? num_frozen_rdp_commands : num_rdp_commands, ##__VA_ARGS__); \
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
    if (rdpq_mode_cpu_track)
        rdpq_shadow_apply_cmd(cmd_id, w0, w1, 0, 0);
    else
        rdpq_mode_write(2, 0, RDPQ_OVL_ID, cmd_id, w0, w1);  // COMBINE+SOM
}

/** @brief Write a fixup that changes the current render mode (12-byte command) */
__attribute__((noinline))
void __rdpq_fixup_mode3(uint32_t cmd_id, uint32_t w0, uint32_t w1, uint32_t w2)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    if (rdpq_mode_cpu_track)
        rdpq_shadow_apply_cmd(cmd_id, w0, w1, w2, 0);
    else
        rdpq_mode_write(2, 0, RDPQ_OVL_ID, cmd_id, w0, w1, w2);  // COMBINE+SOM
}

/** @brief Write a fixup that changes the current render mode (16-byte command) */
__attribute__((noinline))
void __rdpq_fixup_mode4(uint32_t cmd_id, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    if (rdpq_mode_cpu_track)
        rdpq_shadow_apply_cmd(cmd_id, w0, w1, w2, w3);
    else
        rdpq_mode_write(2, 0, RDPQ_OVL_ID, cmd_id, w0, w1, w2, w3);  // COMBINE+SOM
}

/** @brief Write a fixup to reset the render mode */
__attribute__((noinline))
void __rdpq_reset_render_mode(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    // ResetRenderMode can generate: SCISSOR+COMBINE+SOM when not frozen,
    // or just SCISSOR when frozen.
    rdpq_mode_write(3, 1, RDPQ_OVL_ID, RDPQ_CMD_RESET_RENDER_MODE, w0, w1, w2, w3);
}

void rdpq_mode_push(void)
{
    // Push is not a RDP passthrough/fixup command, it's just a standard
    // RSP command. Use rspq_write.
    rspq_write(RDPQ_OVL_ID, RDPQ_CMD_PUSH_RENDER_MODE);
}

void rdpq_mode_pop(void)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    // ModePop can generate: SCISSOR+COMBINE+SOM when not frozen,
    // or just SCISSOR when frozen.
    rdpq_mode_write(3, 1, RDPQ_OVL_ID, RDPQ_CMD_POP_RENDER_MODE);
    rdpq_tracking.cycle_type_known = 0;
}

/** @brief Like #rdpq_set_mode_fill, but without fill color configuration */
void __rdpq_set_mode_fill(void) {
    if (rdpq_mode_cpu_track)
        rdpq_mode_cpu_track = false;
    uint64_t som = (0xEFull << 56) | SOM_CYCLE_FILL;
    __rdpq_reset_render_mode(0, 0, som >> 32, som & 0xFFFFFFFF);
    if (!rdpq_tracking.mode_freeze)
        rdpq_tracking.cycle_type_known = 2;
    else
        rdpq_tracking.cycle_type_frozen = 2;
}

void rdpq_set_mode_copy(bool transparency) {
    if (rdpq_mode_cpu_track)
        rdpq_mode_cpu_track = false;
    uint64_t som = (0xEFull << 56) | SOM_CYCLE_COPY | (transparency ? SOM_ALPHACOMPARE_THRESHOLD : 0);
    __rdpq_reset_render_mode(0, 0, som >> 32, som & 0xFFFFFFFF);
    if (!rdpq_tracking.mode_freeze)
        rdpq_tracking.cycle_type_known = 2;
    else
        rdpq_tracking.cycle_type_frozen = 2;
}

void rdpq_set_mode_standard(void) {
    uint64_t cc = RDPQ_COMBINER1(
        (ZERO, ZERO, ZERO, TEX0), (ZERO, ZERO, ZERO, TEX0)
    );
    uint64_t som =
        SOM_TF0_RGB | SOM_TF1_RGB |
        SOM_RGBDITHER_NONE | SOM_ALPHADITHER_NONE |
        SOM_COVERAGE_DEST_ZAP;

    if (rdpq_tracking.mode_freeze) {
        rdpq_mode_cpu_track = true;
        rdpq_shadow_reset(som);
        rdpq_mode_shadow.combiner = cc;
    } else {
        __rdpq_reset_render_mode(
            cc >> 32,   cc & 0xFFFFFFFF,
            som >> 32, som & 0xFFFFFFFF);
    }
    rdpq_mode_combiner(cc); // FIXME: this should not be required, but we need it for the mipmap mask
    if (!rdpq_tracking.mode_freeze)
        rdpq_tracking.cycle_type_known = 1;
    else
        rdpq_tracking.cycle_type_frozen = 1;
}

void rdpq_set_mode_yuv(bool bilinear) {
    uint64_t cc, som;

    if (rdpq_mode_cpu_track)
        rdpq_mode_cpu_track = false;
    som = SOM_RGBDITHER_NONE | SOM_ALPHADITHER_NONE | SOM_TF0_YUV | SOM_TF1_YUV;
    cc = RDPQ_COMBINER1((TEX0, K4, K5, TEX0), (0, 0, 0, 1));

    __rdpq_reset_render_mode(
        cc >> 32,   cc & 0xFFFFFFFF,
        som >> 32, som & 0xFFFFFFFF);
    if (bilinear) {
        rdpq_mode_filter(FILTER_BILINEAR);
        cc = RDPQ_COMBINER1((TEX1, K4, K5, TEX1), (0, 0, 0, 1));
    }
    rdpq_mode_combiner(cc); // FIXME: this should not be required, but we need it for the mipmap mask

    if (!rdpq_tracking.mode_freeze)
        rdpq_tracking.cycle_type_known = 1;
    else
        rdpq_tracking.cycle_type_frozen = 1;

    // Set the YUV coefficients (FIXME: make this configurable)
    const yuv_colorspace_t *cs = &YUV_BT601_TV;
    rdpq_set_yuv_parms(cs->k0, cs->k1, cs->k2, cs->k3, cs->k4, cs->k5);
}

void rdpq_mode_begin(void)
{
    assertf(!rdpq_tracking.mode_freeze, "rdpq_mode_begin nested calls not supported");
    // Freeze render mode updates. We call rdpq_change_other_modes_raw here
    // (instead of __rdpq_mode_change_som) because there will be no RDP
    // commands emitted from this call.
    rdpq_tracking.mode_freeze = true;
    rdpq_tracking.cycle_type_frozen = 0;
    rdpq_mode_cpu_track = false;
    __rdpq_mode_change_som(SOMX_UPDATE_FREEZE, SOMX_UPDATE_FREEZE);
}

void rdpq_mode_end(void)
{
    if (rdpq_mode_cpu_track) {
        uint64_t comb_final = 0;
        uint64_t som_final = 0;
        rdpq_update_render_mode_cpu(&comb_final, &som_final);
        debugf("[rdpq_mode_end] comb_final=%08lx%08lx som_final=%08lx%08lx\n", 
            (unsigned long)(comb_final >> 32), (unsigned long)(comb_final & 0xFFFFFFFF), 
            (unsigned long)(som_final >> 32), (unsigned long)(som_final & 0xFFFFFFFF));

        rdpq_passthrough_write((RDPQ_CMD_SET_OTHER_MODES_RAW, som_final >> 32, som_final & 0xFFFFFFFF));
        rdpq_passthrough_write((RDPQ_CMD_SET_COMBINE_MODE_RAW, comb_final >> 32, comb_final & 0xFFFFFFFF));
        rspq_int_write(RSPQ_CMD_RDP_RESET_MODE, 0, 
            rdpq_mode_shadow.combiner >> 32, rdpq_mode_shadow.combiner & 0xFFFFFFFF,
            rdpq_mode_shadow.combiner_mipmapmask >> 32, rdpq_mode_shadow.combiner_mipmapmask & 0xFFFFFFFF,
            rdpq_mode_shadow.blend_step0, rdpq_mode_shadow.blend_step1,
            rdpq_mode_shadow.other_modes >> 32, rdpq_mode_shadow.other_modes & 0xFFFFFFFF);

        rdpq_tracking.mode_freeze = false;
        rdpq_tracking.cycle_type_known = rdpq_tracking.cycle_type_frozen;
        rdpq_mode_cpu_track = false;
        return;
    }

    // Unfreeze render mode updates and recalculate new render mode.
    rdpq_tracking.mode_freeze = false;
    rdpq_tracking.cycle_type_known = rdpq_tracking.cycle_type_frozen;
    __rdpq_mode_change_som(SOMX_UPDATE_FREEZE, 0);
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
