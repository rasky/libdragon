#include <math.h>
#include <float.h>
#include "rdpq.h"
#include "rspq.h"
#include "rdpq_internal.h"
#include "utils.h"

#define TRUNCATE_S11_2(x) (((x)&0x1fff) | (((x)>>18)&~0x1fff))

/** @brief Converts a float to a s16.16 fixed point number */
int32_t float_to_s16_16(float f)
{
    // Currently the float must be clamped to this range because
    // otherwise the trunc.w.s instruction can potentially trigger
    // an unimplemented operation exception due to integer overflow.
    // TODO: maybe handle the exception? Clamp the value in the exception handler?
    if (f >= 32768.f) {
        return 0x7FFFFFFF;
    }

    if (f < -32768.f) {
        return 0x80000000;
    }

    return floor(f * 65536.f);
}

typedef struct {
    float hx, hy;
    float mx, my;
    float fy;
    float ish;
    float attr_factor;
} rdpq_tri_edge_data_t;

__attribute__((always_inline))
inline void __rdpq_write_edge_coeffs(rspq_write_t *w, rdpq_tri_edge_data_t *data, uint8_t tile, uint8_t mipmaps, const float *v1, const float *v2, const float *v3)
{
    const float x1 = v1[0];
    const float x2 = v2[0];
    const float x3 = v3[0];
    const float y1 = floorf(v1[1]*4)/4;
    const float y2 = floorf(v2[1]*4)/4;
    const float y3 = floorf(v3[1]*4)/4;

    const float to_fixed_11_2 = 4.0f;
    int32_t y1f = TRUNCATE_S11_2((int32_t)floorf(v1[1]*to_fixed_11_2));
    int32_t y2f = TRUNCATE_S11_2((int32_t)floorf(v2[1]*to_fixed_11_2));
    int32_t y3f = TRUNCATE_S11_2((int32_t)floorf(v3[1]*to_fixed_11_2));

    data->hx = x3 - x1;        
    data->hy = y3 - y1;        
    data->mx = x2 - x1;
    data->my = y2 - y1;
    float lx = x3 - x2;
    float ly = y3 - y2;

    const float nz = (data->hx*data->my) - (data->hy*data->mx);
    data->attr_factor = (fabs(nz) > FLT_MIN) ? (-1.0f / nz) : 0;
    const uint32_t lft = nz < 0;

    data->ish = (fabs(data->hy) > FLT_MIN) ? (data->hx / data->hy) : 0;
    float ism = (fabs(data->my) > FLT_MIN) ? (data->mx / data->my) : 0;
    float isl = (fabs(ly) > FLT_MIN) ? (lx / ly) : 0;
    data->fy = floorf(y1) - y1;

    const float xh = x1 + data->fy * data->ish;
    const float xm = x1 + data->fy * ism;
    const float xl = x2;

    rspq_write_arg(w, _carg(lft, 0x1, 23) | _carg(mipmaps-1, 0x7, 19) | _carg(tile, 0x7, 16) | _carg(y3f, 0x3FFF, 0));
    rspq_write_arg(w, _carg(y2f, 0x3FFF, 16) | _carg(y1f, 0x3FFF, 0));
    rspq_write_arg(w, float_to_s16_16(xl));
    rspq_write_arg(w, float_to_s16_16(isl));
    rspq_write_arg(w, float_to_s16_16(xh));
    rspq_write_arg(w, float_to_s16_16(data->ish));
    rspq_write_arg(w, float_to_s16_16(xm));
    rspq_write_arg(w, float_to_s16_16(ism));
}

__attribute__((always_inline))
static inline void __rdpq_write_shade_coeffs(rspq_write_t *w, rdpq_tri_edge_data_t *data, const float *v1, const float *v2, const float *v3)
{
    const float mr = (v2[0] - v1[0]) * 255.f;
    const float mg = (v2[1] - v1[1]) * 255.f;
    const float mb = (v2[2] - v1[2]) * 255.f;
    const float ma = (v2[3] - v1[3]) * 255.f;
    const float hr = (v3[0] - v1[0]) * 255.f;
    const float hg = (v3[1] - v1[1]) * 255.f;
    const float hb = (v3[2] - v1[2]) * 255.f;
    const float ha = (v3[3] - v1[3]) * 255.f;

    const float nxR = data->hy*mr - data->my*hr;
    const float nxG = data->hy*mg - data->my*hg;
    const float nxB = data->hy*mb - data->my*hb;
    const float nxA = data->hy*ma - data->my*ha;
    const float nyR = data->mx*hr - data->hx*mr;
    const float nyG = data->mx*hg - data->hx*mg;
    const float nyB = data->mx*hb - data->hx*mb;
    const float nyA = data->mx*ha - data->hx*ma;

    const float DrDx = nxR * data->attr_factor;
    const float DgDx = nxG * data->attr_factor;
    const float DbDx = nxB * data->attr_factor;
    const float DaDx = nxA * data->attr_factor;
    const float DrDy = nyR * data->attr_factor;
    const float DgDy = nyG * data->attr_factor;
    const float DbDy = nyB * data->attr_factor;
    const float DaDy = nyA * data->attr_factor;

    const float DrDe = DrDy + DrDx * data->ish;
    const float DgDe = DgDy + DgDx * data->ish;
    const float DbDe = DbDy + DbDx * data->ish;
    const float DaDe = DaDy + DaDx * data->ish;

    const int32_t final_r = float_to_s16_16(v1[0] * 255.f + data->fy * DrDe);
    const int32_t final_g = float_to_s16_16(v1[1] * 255.f + data->fy * DgDe);
    const int32_t final_b = float_to_s16_16(v1[2] * 255.f + data->fy * DbDe);
    const int32_t final_a = float_to_s16_16(v1[3] * 255.f + data->fy * DaDe);

    const int32_t DrDx_fixed = float_to_s16_16(DrDx);
    const int32_t DgDx_fixed = float_to_s16_16(DgDx);
    const int32_t DbDx_fixed = float_to_s16_16(DbDx);
    const int32_t DaDx_fixed = float_to_s16_16(DaDx);

    const int32_t DrDe_fixed = float_to_s16_16(DrDe);
    const int32_t DgDe_fixed = float_to_s16_16(DgDe);
    const int32_t DbDe_fixed = float_to_s16_16(DbDe);
    const int32_t DaDe_fixed = float_to_s16_16(DaDe);

    const int32_t DrDy_fixed = float_to_s16_16(DrDy);
    const int32_t DgDy_fixed = float_to_s16_16(DgDy);
    const int32_t DbDy_fixed = float_to_s16_16(DbDy);
    const int32_t DaDy_fixed = float_to_s16_16(DaDy);

    rspq_write_arg(w, (final_r&0xffff0000) | (0xffff&(final_g>>16)));
    rspq_write_arg(w, (final_b&0xffff0000) | (0xffff&(final_a>>16)));
    rspq_write_arg(w, (DrDx_fixed&0xffff0000) | (0xffff&(DgDx_fixed>>16)));
    rspq_write_arg(w, (DbDx_fixed&0xffff0000) | (0xffff&(DaDx_fixed>>16)));
    rspq_write_arg(w, (final_r<<16) | (final_g&0xffff));
    rspq_write_arg(w, (final_b<<16) | (final_a&0xffff));
    rspq_write_arg(w, (DrDx_fixed<<16) | (DgDx_fixed&0xffff));
    rspq_write_arg(w, (DbDx_fixed<<16) | (DaDx_fixed&0xffff));
    rspq_write_arg(w, (DrDe_fixed&0xffff0000) | (0xffff&(DgDe_fixed>>16)));
    rspq_write_arg(w, (DbDe_fixed&0xffff0000) | (0xffff&(DaDe_fixed>>16)));
    rspq_write_arg(w, (DrDy_fixed&0xffff0000) | (0xffff&(DgDy_fixed>>16)));
    rspq_write_arg(w, (DbDy_fixed&0xffff0000) | (0xffff&(DaDy_fixed>>16)));
    rspq_write_arg(w, (DrDe_fixed<<16) | (DgDe_fixed&0xffff));
    rspq_write_arg(w, (DbDe_fixed<<16) | (DaDe_fixed&0xffff));
    rspq_write_arg(w, (DrDy_fixed<<16) | (DgDy_fixed&&0xffff));
    rspq_write_arg(w, (DbDy_fixed<<16) | (DaDy_fixed&&0xffff));
}

__attribute__((always_inline))
inline void __rdpq_write_tex_coeffs(rspq_write_t *w, rdpq_tri_edge_data_t *data, const float *v1, const float *v2, const float *v3)
{
    float s1 = v1[0] * 32.f, t1 = v1[1] * 32.f, w1 = v1[2];
    float s2 = v2[0] * 32.f, t2 = v2[1] * 32.f, w2 = v2[2];
    float s3 = v3[0] * 32.f, t3 = v3[1] * 32.f, w3 = v3[2];

    const float w_factor = 1.0f / MAX(MAX(w1, w2), w3);

    w1 *= w_factor;
    w2 *= w_factor;
    w3 *= w_factor;

    s1 *= w1;
    t1 *= w1;
    s2 *= w2;
    t2 *= w2;
    s3 *= w3;
    t3 *= w3;

    w1 *= 0x7FFF;
    w2 *= 0x7FFF;
    w3 *= 0x7FFF;

    const float ms = s2 - s1;
    const float mt = t2 - t1;
    const float mw = w2 - w1;
    const float hs = s3 - s1;
    const float ht = t3 - t1;
    const float hw = w3 - w1;

    const float nxS = data->hy*ms - data->my*hs;
    const float nxT = data->hy*mt - data->my*ht;
    const float nxW = data->hy*mw - data->my*hw;
    const float nyS = data->mx*hs - data->hx*ms;
    const float nyT = data->mx*ht - data->hx*mt;
    const float nyW = data->mx*hw - data->hx*mw;

    const float DsDx = nxS * data->attr_factor;
    const float DtDx = nxT * data->attr_factor;
    const float DwDx = nxW * data->attr_factor;
    const float DsDy = nyS * data->attr_factor;
    const float DtDy = nyT * data->attr_factor;
    const float DwDy = nyW * data->attr_factor;

    const float DsDe = DsDy + DsDx * data->ish;
    const float DtDe = DtDy + DtDx * data->ish;
    const float DwDe = DwDy + DwDx * data->ish;

    const int32_t final_s = float_to_s16_16(s1 + data->fy * DsDe);
    const int32_t final_t = float_to_s16_16(t1 + data->fy * DtDe);
    const int32_t final_w = float_to_s16_16(w1 + data->fy * DwDe);

    const int32_t DsDx_fixed = float_to_s16_16(DsDx);
    const int32_t DtDx_fixed = float_to_s16_16(DtDx);
    const int32_t DwDx_fixed = float_to_s16_16(DwDx);

    const int32_t DsDe_fixed = float_to_s16_16(DsDe);
    const int32_t DtDe_fixed = float_to_s16_16(DtDe);
    const int32_t DwDe_fixed = float_to_s16_16(DwDe);

    const int32_t DsDy_fixed = float_to_s16_16(DsDy);
    const int32_t DtDy_fixed = float_to_s16_16(DtDy);
    const int32_t DwDy_fixed = float_to_s16_16(DwDy);

    rspq_write_arg(w, (final_s&0xffff0000) | (0xffff&(final_t>>16)));  
    rspq_write_arg(w, (final_w&0xffff0000));
    rspq_write_arg(w, (DsDx_fixed&0xffff0000) | (0xffff&(DtDx_fixed>>16)));
    rspq_write_arg(w, (DwDx_fixed&0xffff0000));    
    rspq_write_arg(w, (final_s<<16) | (final_t&0xffff));
    rspq_write_arg(w, (final_w<<16));
    rspq_write_arg(w, (DsDx_fixed<<16) | (DtDx_fixed&0xffff));
    rspq_write_arg(w, (DwDx_fixed<<16));
    rspq_write_arg(w, (DsDe_fixed&0xffff0000) | (0xffff&(DtDe_fixed>>16)));
    rspq_write_arg(w, (DwDe_fixed&0xffff0000));
    rspq_write_arg(w, (DsDy_fixed&0xffff0000) | (0xffff&(DtDy_fixed>>16)));
    rspq_write_arg(w, (DwDy_fixed&0xffff0000));
    rspq_write_arg(w, (DsDe_fixed<<16) | (DtDe_fixed&0xffff));
    rspq_write_arg(w, (DwDe_fixed<<16));
    rspq_write_arg(w, (DsDy_fixed<<16) | (DtDy_fixed&&0xffff));
    rspq_write_arg(w, (DwDy_fixed<<16));
}

__attribute__((always_inline))
inline void __rdpq_write_zbuf_coeffs(rspq_write_t *w, rdpq_tri_edge_data_t *data, const float *v1, const float *v2, const float *v3)
{
    const float z1 = v1[0] * 0x7FFF;
    const float z2 = v2[0] * 0x7FFF;
    const float z3 = v3[0] * 0x7FFF;

    const float mz = z2 - z1;
    const float hz = z3 - z1;

    const float nxz = data->hy*mz - data->my*hz;
    const float nyz = data->mx*hz - data->hx*mz;

    const float DzDx = nxz * data->attr_factor;
    const float DzDy = nyz * data->attr_factor;
    const float DzDe = DzDy + DzDx * data->ish;

    const int32_t final_z = float_to_s16_16(z1 + data->fy * DzDe);
    const int32_t DzDx_fixed = float_to_s16_16(DzDx);
    const int32_t DzDe_fixed = float_to_s16_16(DzDe);
    const int32_t DzDy_fixed = float_to_s16_16(DzDy);

    rspq_write_arg(w, final_z);
    rspq_write_arg(w, DzDx_fixed);
    rspq_write_arg(w, DzDe_fixed);
    rspq_write_arg(w, DzDy_fixed);
}

__attribute__((noinline))
void rdpq_triangle(tile_t tile, uint8_t mipmaps, int32_t pos_offset, int32_t shade_offset, int32_t tex_offset, int32_t z_offset, const float *v1, const float *v2, const float *v3)
{
    uint32_t res = AUTOSYNC_PIPE;
    if (tex_offset >= 0) {
        res |= AUTOSYNC_TILE(tile);
    }
    __rdpq_autosync_use(res);

    uint32_t cmd_id = RDPQ_CMD_TRI;

    uint32_t size = 8;
    if (shade_offset >= 0) {
        size += 16;
        cmd_id |= 0x4;
    }
    if (tex_offset >= 0) {
        size += 16;
        cmd_id |= 0x2;
    }
    if (z_offset >= 0) {
        size += 4;
        cmd_id |= 0x1;
    }

    rspq_write_t w = rspq_write_begin(RDPQ_OVL_ID, cmd_id, size);

    if( v1[pos_offset + 1] > v2[pos_offset + 1] ) { SWAP(v1, v2); }
    if( v2[pos_offset + 1] > v3[pos_offset + 1] ) { SWAP(v2, v3); }
    if( v1[pos_offset + 1] > v2[pos_offset + 1] ) { SWAP(v1, v2); }

    rdpq_tri_edge_data_t data;
    __rdpq_write_edge_coeffs(&w, &data, tile, mipmaps, v1 + pos_offset, v2 + pos_offset, v3 + pos_offset);

    if (shade_offset >= 0) {
        __rdpq_write_shade_coeffs(&w, &data, v1 + shade_offset, v2 + shade_offset, v3 + shade_offset);
    }

    if (tex_offset >= 0) {
        __rdpq_write_tex_coeffs(&w, &data, v1 + tex_offset, v2 + tex_offset, v3 + tex_offset);
    }

    if (z_offset >= 0) {
        __rdpq_write_zbuf_coeffs(&w, &data, v1 + z_offset, v2 + z_offset, v3 + z_offset);
    }

    rspq_write_end(&w);
}
