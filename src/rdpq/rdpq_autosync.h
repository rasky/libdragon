#ifndef LIBDRAGON_RDPQ_AUTOSYNC_H
#define LIBDRAGON_RDPQ_AUTOSYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void __rdpq_autosync_wait_after_draw(int num_clk);
void __rdpq_autosync_wait_after_draw_tmem(int num_clk);
void __rdpq_autosync_wait_after_draw_tile(int num_clk, int tile);
void __rdpq_autosync_wait_after_tmem_write(int num_clk);

typedef struct {
    uint32_t gclk;
    uint32_t last_draw;         // last draw command (any)
    uint32_t last_draw_tex;     // last draw command (textured: use TMEM and tile)
    uint32_t last_tmem_write;   // last TMEM upload command
    uint8_t last_tile_usage;
} rdpq_autosync_t;

extern rdpq_autosync_t rdpq_autosync;

// When a draw command is issued, we increment GCLK by a fixed amount to simulate
// the drawing. This is enough to make sure all previous commands are not causing
// syncs anymore.
// FIXME: this is wrong for contrieved situations: for instance one could issue
// a LOAD_TILE, then a very fast FILL_RECT 1x1 (probably taking 1 cycle),
// and then a TEX_RECT that would require a sync with the previous LOAD_TILE.
#define __AUTOSYNC_FAKEDRAW  64

static inline void __rdpq_autosync_draw_notex(void)
{
    rdpq_autosync.gclk += __AUTOSYNC_FAKEDRAW;
    rdpq_autosync.last_draw = rdpq_autosync.gclk;
}

static inline void __rdpq_autosync_draw_tex(int firsttile, int numtiles)
{
    __rdpq_autosync_wait_after_tmem_write(19);

    int tilemask = (1 << numtiles) - 1;
    tilemask = tilemask << (firsttile & 7);
    tilemask = (tilemask & 0xFF) | (tilemask >> 8);

    rdpq_autosync.gclk += __AUTOSYNC_FAKEDRAW;
    rdpq_autosync.last_draw_tex = rdpq_autosync.gclk;
    rdpq_autosync.last_tile_usage = tilemask;
}

static inline void __rdpq_autosync_fillrect(void) { __rdpq_autosync_draw_notex(); }
static inline void __rdpq_autosync_tri_notex(void) { __rdpq_autosync_draw_notex();}

static inline void __rdpq_autosync_texrect(int firsttile, int numtiles) { __rdpq_autosync_draw_tex(firsttile, numtiles); }
static inline void __rdpq_autosync_tri_tex(int firsttile, int numtiles) { __rdpq_autosync_draw_tex(firsttile, numtiles); }

extern void __rdpq_autosync_outline(uint32_t cmd_bitmask);

__attribute__((always_inline))
static inline void __rdpq_autosync_inline(uint32_t cmd_bitmask)
{
    int wait_after_draw = 0;
    if (cmd_bitmask & RDPQ_AUTOSYNC_SET_FILL_COLOR) wait_after_draw = MAX(29, wait_after_draw);
    if (wait_after_draw) 
        __rdpq_autosync_wait_after_draw(wait_after_draw);

    int wait_after_tmem_write = 0;
    


}


/*

Group LOAD: LOAD_BLOCK, LOAD_TLUT, LOAD_TILE
Group TEX_DRAW: TEX_RECT, TRI_TEX, TRI_TEX_Z, TRI_TEX_SHADE, TRI_TEX_SHADE_Z
Group DRAW: FILL_RECT, TRI, TRI_Z, TRI_SHADE, TRI_SHADE_Z
Group PIPE: SET_OTHER_MODES, SET_COMBINER, SET_ENV_COLOR, SET_BLEND_COLOR, SET_FOG_COLOR, SET_FILL_COLOR, SET_CONVERT, SET_KEY_R, SET_KEY_GB
Group TEXIMG: SET_TEX_IMAGE 
Group TILE: SET_TILE, SET_TILE_SIZE
Group NOSYNC: SET_SCISSOR, SET_COLOR_IMAGE, SET_Z_IMAGE, SET_PRIM_COLOR, SET_PRIM_DEPTH, SYNC_TILE, SYNC_LOAD, SYNC_PIPE, SYNC_FULL

Conflicts:
DRAW/PIPE        : Modifying an attribute that is actively used will corrupt the previous primitive
TEX_DRAW/PIPE    : As above ^
TEX_DRAW/TILE    : Modifying the tile used for rendering during texture primitives will corrupt the previous primitive
TEX_DRAW/LOAD(1) : Loading before previous primitive completes may cause it to sample new data
TEX_DRAW/LOAD(2) : Loading before previous primitive completes may change the tile used for rendering
LOAD/TEX_DRAW    : Texture primitives may sample TMEM before the previous load is complete
LOAD/TILE        : Modifying the tile used for loading during loading will corrupt the previous load
LOAD/TEXIMG      : Modifying the texture image pointer while a load is in progress will corrupt it

Note: SYNC_FULL must be the last command in a DMA block, and no other DMA block
should be started before SYNC_FULL completes (the DP interrupt is triggered).
*/



#ifdef __cplusplus
}
#endif

#endif