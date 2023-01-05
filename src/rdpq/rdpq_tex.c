/**
 * @file rdpq_tex.c
 * @brief RDP Command queue: texture loading
 * @ingroup rdp
 */

#define _GNU_SOURCE
#include "rdpq.h"
#include "rdpq_tri.h"
#include "rdpq_tex.h"
#include "utils.h"
#include <math.h>

/** @brief Address in TMEM where the palettes must be loaded */
#define TMEM_PALETTE_ADDR   0x800

enum tex_load_mode {
    TEX_LOAD_UNKNOWN,
    TEX_LOAD_TILE,
    TEX_LOAD_BLOCK,
};

typedef struct tex_loader_s {
    surface_t *tex;
    rdpq_tile_t tile;
    struct {
        int width, height;
        int num_texels, tmem_pitch;
        bool can_load_block;
    } rect;
    int tmem_addr;
    int tlut;
    enum tex_load_mode load_mode;
    void (*load_block)(struct tex_loader_s *tload, int s0, int t0, int s1, int t1);
    void (*load_tile)(struct tex_loader_s *tload, int s0, int t0, int s1, int t1);
} tex_loader_t;

static int texload_set_rect(tex_loader_t *tload, int s0, int t0, int s1, int t1)
{
    tex_format_t fmt = surface_get_format(tload->tex);
    if (TEX_FORMAT_BITDEPTH(fmt) == 4) {
        s0 &= ~1; s1 = (s1+1) & ~1;
    }

    int width = s1 - s0;
    int height = t1 - t0;

    if (width != tload->rect.width || height != tload->rect.height) {
        if (width != tload->rect.width) {
            int pitch_shift = fmt == FMT_RGBA32 ? 1 : 0;
            int stride_mask = fmt == FMT_RGBA32 ? 15 : 7;
            tload->rect.tmem_pitch = ROUND_UP(TEX_FORMAT_PIX2BYTES(fmt, width) >> pitch_shift, 8);
            tload->rect.can_load_block = 
                tload->tile != RDPQ_TILE_INTERNAL && 
                TEX_FORMAT_PIX2BYTES(fmt, width) == tload->tex->stride &&
                (tload->tex->stride & stride_mask) == 0;
            tload->load_mode = TEX_LOAD_UNKNOWN;
        }
        int tmem_size = (fmt == FMT_RGBA32 || fmt == FMT_CI4 || fmt == FMT_CI8) ? 2048 : 4096;
        assertf(height * tload->rect.tmem_pitch <= tmem_size,
            "A rectangle of size %dx%d format %s is too big to fit in TMEM", width, height, tex_format_name(fmt));
        tload->rect.width = width;
        tload->rect.height = height;
        tload->rect.num_texels = width * height;
    }
    return tload->rect.tmem_pitch * height;
}

static int tex_loader_load(tex_loader_t *tload, int s0, int t0, int s1, int t1)
{
    int mem = texload_set_rect(tload, s0, t0, s1, t1);
    if (tload->rect.can_load_block && (t0 & 1) == 0)
        tload->load_block(tload, s0, t0, s1, t1);
    else
        tload->load_tile(tload, s0, t0, s1, t1);
    return mem;
}

static void tex_loader_set_tmem_addr(tex_loader_t *tload, int tmem_addr)
{
    tload->tmem_addr = tmem_addr;
    tload->load_mode = TEX_LOAD_UNKNOWN;
}

static void tex_loader_set_tlut(tex_loader_t *tload, int tlut)
{
    tload->tlut = tlut;
    tload->load_mode = TEX_LOAD_UNKNOWN;
}

static int texload_calc_max_height(tex_loader_t *tload, int s0, int s1)
{
    texload_set_rect(tload, s0, 0, s1, 1);

    tex_format_t fmt = surface_get_format(tload->tex);
    int tmem_size = (fmt == FMT_RGBA32 || fmt == FMT_CI4 || fmt == FMT_CI8) ? 2048 : 4096;
    return tmem_size / tload->rect.tmem_pitch;
}

static void texload_block_4bpp(tex_loader_t *tload, int s0, int t0, int s1, int t1)
{
    if (tload->load_mode != TEX_LOAD_BLOCK) {
        // Use LOAD_BLOCK if we are uploading a full texture. Notice the weirdness of LOAD_BLOCK:
        // * SET_TILE must be configured with tmem_pitch=0, as that is weirdly used as the number of
        //   texels to skip per line, which we don't need.
        rdpq_set_texture_image_raw(0, PhysicalAddr(tload->tex->buffer), FMT_RGBA16, tload->tex->width, tload->tex->height);
        rdpq_set_tile(RDPQ_TILE_INTERNAL, FMT_RGBA16, tload->tmem_addr, 0, 0);
        rdpq_set_tile(tload->tile, surface_get_format(tload->tex), tload->tmem_addr, tload->rect.tmem_pitch, tload->tlut);
        tload->load_mode = TEX_LOAD_BLOCK;
    }

    s0 &= ~1; s1 = (s1+1) & ~1;
    rdpq_load_block(RDPQ_TILE_INTERNAL, s0/2, t0, tload->rect.num_texels/4, tload->rect.tmem_pitch);
    rdpq_set_tile_size(tload->tile, s0, t0, s1, t1);
}

static void texload_tile_4bpp(tex_loader_t *tload, int s0, int t0, int s1, int t1)
{
    if (tload->load_mode != TEX_LOAD_TILE) {
        rdpq_set_texture_image_raw(0, PhysicalAddr(tload->tex->buffer), FMT_CI8, tload->tex->stride, tload->tex->height);
        rdpq_set_tile(RDPQ_TILE_INTERNAL, FMT_CI8, tload->tmem_addr, tload->rect.tmem_pitch, 0);
        rdpq_set_tile(tload->tile, surface_get_format(tload->tex), tload->tmem_addr, tload->rect.tmem_pitch, tload->tlut);
    }

    s0 &= ~1; s1 = (s1+1) & ~1;
    rdpq_load_tile(RDPQ_TILE_INTERNAL, s0/2, t0, s1/2, t1);
    rdpq_set_tile_size(tload->tile, s0, t0, s1, t1);
}

static void texload_block(tex_loader_t *tload, int s0, int t0, int s1, int t1)
{
    tex_format_t fmt = surface_get_format(tload->tex);

    if (tload->load_mode != TEX_LOAD_BLOCK) {
        // Use LOAD_BLOCK if we are uploading a full texture. Notice the weirdness of LOAD_BLOCK:
        // * SET_TILE must be configured with tmem_pitch=0, as that is weirdly used as the number of
        //   texels to skip per line, which we don't need.
        rdpq_set_texture_image_raw(0, PhysicalAddr(tload->tex->buffer), fmt, tload->tex->width, tload->tex->height);
        rdpq_set_tile(RDPQ_TILE_INTERNAL, fmt, tload->tmem_addr, 0, 0);
        rdpq_set_tile(tload->tile, fmt, tload->tmem_addr, tload->rect.tmem_pitch, tload->tlut);
        tload->load_mode = TEX_LOAD_BLOCK;
    }

    rdpq_load_block(RDPQ_TILE_INTERNAL, s0, t0, tload->rect.num_texels, (fmt == FMT_RGBA32) ? tload->rect.tmem_pitch*2 : tload->rect.tmem_pitch);
    rdpq_set_tile_size(tload->tile, s0, t0, s1, t1);
}

static void texload_tile(tex_loader_t *tload, int s0, int t0, int s1, int t1)
{
    tex_format_t fmt = surface_get_format(tload->tex);

    if (tload->load_mode != TEX_LOAD_TILE) {
        rdpq_set_texture_image(tload->tex);
        rdpq_set_tile(tload->tile, fmt, tload->tmem_addr, tload->rect.tmem_pitch, tload->tlut);
        tload->load_mode = TEX_LOAD_TILE;
    }

    rdpq_load_tile(tload->tile, s0, t0, s1, t1);
}

static tex_loader_t tex_loader_init(rdpq_tile_t tile, surface_t *tex) {
    bool is_4bpp = TEX_FORMAT_BITDEPTH(surface_get_format(tex)) == 4;
    return (tex_loader_t){
        .tex = tex,
        .tile = tile,
        .load_block = is_4bpp ? texload_block_4bpp : texload_block,
        .load_tile = is_4bpp ? texload_tile_4bpp : texload_tile,
    };
}

int rdpq_tex_load_sub_ci4(rdpq_tile_t tile, surface_t *tex, int tmem_addr, int tlut, int s0, int t0, int s1, int t1)
{
    tex_loader_t tload = tex_loader_init(tile, tex);
    tex_loader_set_tlut(&tload, tlut);
    tex_loader_set_tmem_addr(&tload, tmem_addr);
    return tex_loader_load(&tload, s0, t0, s1, t1);
#   
}

int rdpq_tex_load_ci4(rdpq_tile_t tile, surface_t *tex, int tmem_addr, int tlut)
{
    return rdpq_tex_load_sub_ci4(tile, tex, tmem_addr, tlut, 0, 0, tex->width, tex->height);
}

int rdpq_tex_load_sub(rdpq_tile_t tile, surface_t *tex, int tmem_addr, int s0, int t0, int s1, int t1)
{
    tex_loader_t tload = tex_loader_init(tile, tex);
    tex_loader_set_tmem_addr(&tload, tmem_addr);
    return tex_loader_load(&tload, s0, t0, s1, t1);
}

int rdpq_tex_load(rdpq_tile_t tile, surface_t *tex, int tmem_addr)
{
    return rdpq_tex_load_sub(tile, tex, tmem_addr, 0, 0, tex->width, tex->height);
}

/**
 * @brief Helper function to draw a large surface that doesn't fit in TMEM.
 * 
 * This function analyzes the surface, finds the optimal splitting strategy to
 * divided into rectangles that fit TMEM, and then go through them one of by one,
 * loading them into TMEM and drawing them.
 * 
 * The actual drawing is done by the caller, through the draw_cb function. This
 * function will just call it with the information on the current rectangle
 * within the original surface.
 * 
 * @param tile          Hint of the tile to use. Note that this function is free to use
 *                      other tiles to perform its job.
 * @param tex           Surface to draw
 * @param draw_cb       Callback function to draw rectangle by rectangle. It will be called
 *                      with the tile to use for drawing, and the rectangle of the original
 *                      surface that has been loaded into TMEM.
 */
static void tex_draw_split(rdpq_tile_t tile, surface_t *tex, int s0, int t0, int s1, int t1, 
    void (*draw_cb)(rdpq_tile_t tile, int s0, int t0, int s1, int t1))
{
    // The most efficient way to split a large surface is to load it in horizontal strips,
    // whose height maximizes TMEM usage. The last strip might be smaller than the others.

    // Initial configuration of texloader
    tex_loader_t tload = tex_loader_init(tile, tex);

    // Calculate the optimal height for a strip, based on strips of maximum length.
    int tile_h = texload_calc_max_height(&tload, s0, s1);
    
    // Go through the surface
    while (t0 < t1) 
    {
        // Calculate the height of the current strip
        int sn = s1;
        int tn = MIN(t0 + tile_h, t1);

        // Load the current strip
        tex_loader_load(&tload, s0, t0, sn, tn);

        // Call the draw callback for this strip
        draw_cb(tile, s0, t0, sn, tn);

        // Move to the next strip
        t0 = tn;
    }
}

void rdpq_tex_blit(rdpq_tile_t tile, surface_t *tex, int x0, int y0, int screen_width, int screen_height)
{
    float scalex = (float)screen_width / (float)tex->width;
    float scaley = (float)screen_height / (float)tex->height;
    float dsdx = 1.0f / scalex;
    float dtdy = 1.0f / scaley;

    void draw_cb(rdpq_tile_t tile, int s0, int t0, int s1, int t1)
    {
        rdpq_texture_rectangle(tile, 
            x0 + s0 * scalex, y0 + t0 * scaley,
            x0 + s1 * scalex, y0 + t1 * scaley,
            s0, t0, dsdx, dtdy);
    }

    tex_draw_split(tile, tex, 0, 0, tex->width, tex->height, draw_cb);
}

void rdpq_tex_xblit(rdpq_tile_t tile, surface_t *surf, int x0, int y0, const rdpq_blitparms_t *parms)
{
    static const rdpq_blitparms_t default_parms = {0};
    if (!parms) parms = &default_parms;

    int src_width = parms->width ? parms->width : surf->width;
    int src_height = parms->height ? parms->height : surf->height;
    int s0 = parms->s0;
    int t0 = parms->t0;
    int cx = parms->cx;
    int cy = parms->cy;
    float scalex = parms->scale_x == 0 ? 1.0f : parms->scale_x;
    float scaley = parms->scale_y == 0 ? 1.0f : parms->scale_y;
    float dsdx, dtdy; 

    bool rotate = true; //parms->theta != 0.0f;
    float sin_theta, cos_theta; 
    if (rotate) {
        sincosf(parms->theta, &sin_theta, &cos_theta);
    } else {
        sin_theta = 0.0f; cos_theta = 1.0f;
        dsdx = 1.0f / scalex;
        dtdy = 1.0f / scaley;
        if (parms->flip_x) dsdx = -dsdx;
        if (parms->flip_y) dtdy = -dtdy;
    }

    // Translation before all transformations
    float ox = parms->ox;
    float oy = parms->oy;

    float mtx[3][2] = {
        { cos_theta * scalex, -sin_theta * scaley },
        { sin_theta * scalex, cos_theta * scaley },
        { x0 - cx * cos_theta * scalex - cy * sin_theta * scaley,
          y0 + cx * sin_theta * scalex - cy * cos_theta * scaley }
    };

    void draw_cb(rdpq_tile_t tile, int s0, int t0, int s1, int t1)
    {
        if (!rotate) {
            int ks0 = s0, kt0 = t0, ks1 = s1, kt1 = t1;

            if ((scalex < 0) ^ parms->flip_x) { ks0 = src_width - s1; ks1 = src_width - s0; s0 = s1-1; }
            if ((scaley < 0) ^ parms->flip_y) { kt0 = src_height - t1; kt1 = src_height - t0; t0 = t1-1; }

            float k0x = mtx[0][0] * ks0 + mtx[1][0] * kt0 + mtx[2][0];
            float k0y = mtx[0][1] * ks0 + mtx[1][1] * kt0 + mtx[2][1];
            float k2x = mtx[0][0] * ks1 + mtx[1][0] * kt1 + mtx[2][0];
            float k2y = mtx[0][1] * ks1 + mtx[1][1] * kt1 + mtx[2][1];

            rdpq_texture_rectangle(tile, k0x, k0y, k2x, k2y, s0, t0, dsdx, dtdy);
        } else {
            int ks0 = s0 + ox, kt0 = t0 + oy, ks1 = s1 + ox, kt1 = t1 + oy;

            if (parms->flip_x) { ks0 = src_width - ks0; ks1 = src_width - ks1; }
            if (parms->flip_y) { kt0 = src_height - kt0; kt1 = src_height - kt1; }

            float k0x = mtx[0][0] * ks0 + mtx[1][0] * kt0 + mtx[2][0];
            float k0y = mtx[0][1] * ks0 + mtx[1][1] * kt0 + mtx[2][1];
            float k2x = mtx[0][0] * ks1 + mtx[1][0] * kt1 + mtx[2][0];
            float k2y = mtx[0][1] * ks1 + mtx[1][1] * kt1 + mtx[2][1];
            float k1x = mtx[0][0] * ks1 + mtx[1][0] * kt0 + mtx[2][0];
            float k1y = mtx[0][1] * ks1 + mtx[1][1] * kt0 + mtx[2][1];
            float k3x = mtx[0][0] * ks0 + mtx[1][0] * kt1 + mtx[2][0];
            float k3y = mtx[0][1] * ks0 + mtx[1][1] * kt1 + mtx[2][1];

            float v0[5] = { k0x, k0y, s0, t0, 1.0f };
            float v1[5] = { k1x, k1y, s1, t0, 1.0f };
            float v2[5] = { k2x, k2y, s1, t1, 1.0f };
            float v3[5] = { k3x, k3y, s0, t1, 1.0f };
            rdpq_triangle(&TRIFMT_TEX, v0, v1, v2);
            rdpq_triangle(&TRIFMT_TEX, v0, v2, v3);
        }
    }

    tex_draw_split(tile, surf, s0, t0, s0 + src_width, t0 + src_height, draw_cb);
}

void rdpq_tex_load_tlut(uint16_t *tlut, int color_idx, int num_colors)
{
    rdpq_set_texture_image_raw(0, PhysicalAddr(tlut), FMT_RGBA16, num_colors, 1);
    rdpq_set_tile(RDPQ_TILE_INTERNAL, FMT_I4, TMEM_PALETTE_ADDR + color_idx*16*2*4, num_colors, 0);
    rdpq_load_tlut(RDPQ_TILE_INTERNAL, color_idx, num_colors);
}
