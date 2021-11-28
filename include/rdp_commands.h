#ifndef RDP_COMMANDS_H
#define RDP_COMMANDS_H

#define RDP_TILE_FORMAT_RGBA  0
#define RDP_TILE_FORMAT_YUV   1
#define RDP_TILE_FORMAT_INDEX 2
#define RDP_TILE_FORMAT_IA    3
#define RDP_TILE_FORMAT_I     4

#define RDP_TILE_SIZE_4BIT  0
#define RDP_TILE_SIZE_8BIT  1
#define RDP_TILE_SIZE_16BIT 2
#define RDP_TILE_SIZE_32BIT 3

#define RDP_COLOR16(r,g,b,a) ((uint32_t)(((r)<<11)|((g)<<6)|((b)<<1)|(a)))
#define RDP_COLOR32(r,g,b,a) ((uint32_t)(((r)<<24)|((g)<<16)|((b)<<8)|(a)))

// When compiling C/C++ code, 64-bit immediate operands require explicit
// casting to a 64-bit type
#ifdef __ASSEMBLER__
#define cast64(x) (x)
#else
#include <stdint.h>
#define cast64(x) (uint64_t)(x)
#endif

#define RdpSetClippingFX(x0,y0,x1,y1) \
    ((cast64(0x2D))<<56 | (cast64(x0)<<44) | (cast64(y0)<<32) | ((x1)<<12) | ((y1)<<0))
#define RdpSetClippingI(x0,y0,x1,y1) RdpSetClippingFX((x0)<<2, (y0)<<2, (x1)<<2, (y1)<<2)
#define RdpSetClippingF(x0,y0,x1,y1) RdpSetClippingFX((int)((x0)*4), (int)((y0)*4), (int)((x1)*4), (int)((y1)*4))

#define RdpSetConvert(k0,k1,k2,k3,k4,k5) \
    ((cast64(0x2C)<<56) | ((cast64((k0))&0x1FF)<<45) | ((cast64((k1))&0x1FF)<<36) | ((cast64((k2))&0x1FF)<<27) | ((cast64((k3))&0x1FF)<<18) | ((cast64((k4))&0x1FF)<<9) | ((cast64((k5))&0x1FF)<<0))

#define RdpSetTile(fmt, size, line, addr, tidx) \
    ((cast64(0x35)<<56) | (cast64((fmt)) << 53) | (cast64((size)) << 51) | (cast64((line)) << 41) | (cast64((addr)) << 32) | ((tidx) << 24))

#ifndef __ASSEMBLER__
    #define RdpSetTexImage(fmt, size, addr, width) \
        ({ \
            assertf(size != RDP_TILE_SIZE_4BIT, "RdpSetTexImage cannot be called with RDP_TILE_SIZE_4BIT"); \
            ((cast64(0x3D)<<56) | ((addr) & 0x3FFFFF) | (cast64(((width))-1)<<32) | (cast64((fmt))<<53) | (cast64((size))<<51)); \
        })
#else
    #define RdpSetTexImage(fmt, size, addr, width) \
        ((cast64(0x3D)<<56) | ((addr) & 0x3FFFFF) | (cast64(((width))-1)<<32) | (cast64((fmt))<<53) | (cast64((size))<<51))
#endif

#define RdpLoadTileFX(tidx,s0,t0,s1,t1) \
    ((cast64(0x34)<<56) | (cast64((tidx))<<24) | (cast64((s0))<<44) | (cast64((t0))<<32) | ((s1)<<12) | ((t1)<<0))
#define RdpLoadTileI(tidx,s0,t0,s1,t1) RdpLoadTileFX(tidx, (s0)<<2, (t0)<<2, (s1)<<2, (t1)<<2)

#define RdpLoadTlut(tidx, lowidx, highidx) \
    ((cast64(0x30)<<56) | (cast64(tidx) << 24) | (cast64(lowidx)<<46) | (cast64(highidx)<<14))

#define RdpSetTileSizeFX(tidx,s0,t0,s1,t1) \
    ((cast64(0x32)<<56) | ((tidx)<<24) | (cast64(s0)<<44) | (cast64(t0)<<32) | ((s1)<<12) | ((t1)<<0))
#define RdpSetTileSizeI(tidx,s0,t0,s1,t1) \
    RdpSetTileSizeFX(tidx, (s0)<<2, (t0)<<2, (s1)<<2, (t1)<<2)

#define RdpTextureRectangle1FX(tidx,x0,y0,x1,y1) \
    ((cast64(0x24)<<56) | (cast64((x1)&0xFFF)<<44) | (cast64((y1)&0xFFF)<<32) | ((tidx)<<24) | (((x0)&0xFFF)<<12) | (((y0)&0xFFF)<<0))
#define RdpTextureRectangle1I(tidx,x0,y0,x1,y1) \
    RdpTextureRectangle1FX(tidx, (x0)<<2, (y0)<<2, (x1)<<2, (y1)<<2)
#define RdpTextureRectangle1F(tidx,x0,y0,x1,y1) \
    RdpTextureRectangle1FX(tidx, (int32_t)((x0)*4.f), (int32_t)((y0)*4.f), (int32_t)((x1)*4.f), (int32_t)((y1)*4.f))

#define RdpTextureRectangle2FX(s,t,ds,dt) \
    ((cast64((s)&0xFFFF)<<48) | (cast64((t)&0xFFFF)<<32) | (cast64((ds)&0xFFFF)<<16) | (cast64((dt)&0xFFFF)<<0))
#define RdpTextureRectangle2I(s,t,ds,dt) \
    RdpTextureRectangle2FX((s)<<5, (t)<<5, (ds)<<10, (dt)<<10)
#define RdpTextureRectangle2F(s,t,ds,dt) \
    RdpTextureRectangle2FX((int32_t)((s)*32.f), (int32_t)((t)*32.f), (int32_t)((ds)*1024.f), (int32_t)((dt)*1024.f))

#define RdpSetColorImage(fmt, size, width, addr) \
    ((cast64(0x3f)<<56) | ((fmt)<<53) | ((size)<<51) | (((width)-1)<<32) | ((addr)<<0))

#define RdpFillRectangleFX(x0,y0,x1,y1) \
    ((cast64(0x36)<<56) | ((x0)<<12) | ((y0)<<0) | (cast64(x1)<<44) | (cast64(y1)<<32))
#define RdpFillRectangleI(x0,y0,x1,y1) RdpFillRectangleFX((x0)<<2, (y0)<<2, (x1)<<2, (y1)<<2)
#define RdpFillRectangleF(x0,y0,x1,y1) RdpFillRectangleFX((int)((x0)*4), (int)((y0)*4), (int)((x1)*4), (int)((y1)*4))

#define RdpSetFillColor16(color) \
    (((cast64(0x37))<<56) | (cast64(color)<<16) | cast64(color))

#define RdpSetFillColor(color) \
    (((cast64(0x37))<<56) | cast64(color))

#define RdpSetBlendColor(r,g,b,a) \
    (((cast64(0x39))<<56) | cast64(RDP_COLOR32(r,g,b,a)))

#define RdpSetFogColor(r,g,b,a) \
    (((cast64(0x38))<<56) | cast64(RDP_COLOR32(r,g,b,a)))

#define RdpSetPrimColor(color) \
    (((cast64(0x3A))<<56) | cast64(color))

#define RdpSetEnvColor(color) \
    (((cast64(0x3B))<<56) | cast64(color))

#define RdpSyncFull() \
    (cast64(0x29)<<56)
#define RdpSyncLoad() \
    (cast64(0x26)<<56)
#define RdpSyncPipe() \
    (cast64(0x27)<<56)
#define RdpSyncTile() \
    (cast64(0x28)<<56)

/*************************************************************************
 * Color combiner
 *************************************************************************/

#define RDP_COMB_RGB_SUBA_COMBINED  cast64(0)
#define RDP_COMB_RGB_SUBA_TEX0      cast64(1)
#define RDP_COMB_RGB_SUBA_TEX1      cast64(2)
#define RDP_COMB_RGB_SUBA_PRIM      cast64(3)
#define RDP_COMB_RGB_SUBA_SHADE     cast64(4)
#define RDP_COMB_RGB_SUBA_ENV       cast64(5)
#define RDP_COMB_RGB_SUBA_ONE       cast64(6)
#define RDP_COMB_RGB_SUBA_NOISE     cast64(7)
#define RDP_COMB_RGB_SUBA_ZERO      cast64(8)

#define RDP_COMB_RGB_SUBB_COMBINED  cast64(0)
#define RDP_COMB_RGB_SUBB_TEX0      cast64(1)
#define RDP_COMB_RGB_SUBB_TEX1      cast64(2)
#define RDP_COMB_RGB_SUBB_PRIM      cast64(3)
#define RDP_COMB_RGB_SUBB_SHADE     cast64(4)
#define RDP_COMB_RGB_SUBB_ENV       cast64(5)
#define RDP_COMB_RGB_SUBB_KEYCENTER cast64(6)
#define RDP_COMB_RGB_SUBB_K4        cast64(7)
#define RDP_COMB_RGB_SUBB_ZERO      cast64(8)

#define RDP_COMB_RGB_MUL_COMBINED       cast64(0)
#define RDP_COMB_RGB_MUL_TEX0           cast64(1)
#define RDP_COMB_RGB_MUL_TEX1           cast64(2)
#define RDP_COMB_RGB_MUL_PRIM           cast64(3)
#define RDP_COMB_RGB_MUL_SHADE          cast64(4)
#define RDP_COMB_RGB_MUL_ENV            cast64(5)
#define RDP_COMB_RGB_MUL_KEYSCALE       cast64(6)
#define RDP_COMB_RGB_MUL_COMBINED_ALPHA cast64(7)
#define RDP_COMB_RGB_MUL_TEX0_ALPHA     cast64(8)
#define RDP_COMB_RGB_MUL_TEX1_ALPHA     cast64(9)
#define RDP_COMB_RGB_MUL_PRIM_ALPHA     cast64(10)
#define RDP_COMB_RGB_MUL_SHADE_ALPHA    cast64(11)
#define RDP_COMB_RGB_MUL_ENV_ALPHA      cast64(12)
#define RDP_COMB_RGB_MUL_LOD_FRAC       cast64(13)
#define RDP_COMB_RGB_MUL_PRIM_LOD_FRAC  cast64(14)
#define RDP_COMB_RGB_MUL_K5             cast64(15)
#define RDP_COMB_RGB_MUL_ZERO           cast64(16)

#define RDP_COMB_RGB_ADD_COMBINED  cast64(0)
#define RDP_COMB_RGB_ADD_TEX0      cast64(1)
#define RDP_COMB_RGB_ADD_TEX1      cast64(2)
#define RDP_COMB_RGB_ADD_PRIM      cast64(3)
#define RDP_COMB_RGB_ADD_SHADE     cast64(4)
#define RDP_COMB_RGB_ADD_ENV       cast64(5)
#define RDP_COMB_RGB_ADD_ONE       cast64(6)
#define RDP_COMB_RGB_ADD_ZERO      cast64(7)

#define RDP_COMB_ALPHA_ADDSUB_COMBINED  cast64(0)
#define RDP_COMB_ALPHA_ADDSUB_TEX0      cast64(1)
#define RDP_COMB_ALPHA_ADDSUB_TEX1      cast64(2)
#define RDP_COMB_ALPHA_ADDSUB_PRIM      cast64(3)
#define RDP_COMB_ALPHA_ADDSUB_SHADE     cast64(4)
#define RDP_COMB_ALPHA_ADDSUB_ENV       cast64(5)
#define RDP_COMB_ALPHA_ADDSUB_ONE       cast64(6)
#define RDP_COMB_ALPHA_ADDSUB_ZERO      cast64(7)

#define RDP_COMB_ALPHA_MUL_LOD_FRAC         cast64(0)
#define RDP_COMB_ALPHA_MUL_TEX0             cast64(1)
#define RDP_COMB_ALPHA_MUL_TEX1             cast64(2)
#define RDP_COMB_ALPHA_MUL_PRIM             cast64(3)
#define RDP_COMB_ALPHA_MUL_SHADE            cast64(4)
#define RDP_COMB_ALPHA_MUL_ENV              cast64(5)
#define RDP_COMB_ALPHA_MUL_PRIM_LOD_FRAC    cast64(6)
#define RDP_COMB_ALPHA_MUL_ZERO             cast64(7)

#define RDP__Comb1_Rgb(suba, subb, mul, add) \
    ((RDP_COMB_RGB_SUBA_ ## suba) << 52) | \
    ((RDP_COMB_RGB_SUBB_ ## subb) << 28) | \
    ((RDP_COMB_RGB_MUL_  ## mul)  << 47) | \
    ((RDP_COMB_RGB_ADD_  ## add)  << 15)
#define RDP__Comb2_Rgb(suba, subb, mul, add) \
    ((RDP_COMB_RGB_SUBA_ ## suba) << 37) | \
    ((RDP_COMB_RGB_SUBB_ ## subb) << 24) | \
    ((RDP_COMB_RGB_MUL_  ## mul)  << 32) | \
    ((RDP_COMB_RGB_ADD_  ## add)  <<  6)
#define RDP__Comb1_Alpha(suba, subb, mul, add) \
    ((RDP_COMB_ALPHA_ADDSUB_ ## suba) << 44) | \
    ((RDP_COMB_ALPHA_ADDSUB_ ## subb) << 12) | \
    ((RDP_COMB_ALPHA_MUL_    ## mul)  << 41) | \
    ((RDP_COMB_ALPHA_ADDSUB_ ## add)  << 9)
#define RDP__Comb2_Alpha(suba, subb, mul, add) \
    ((RDP_COMB_ALPHA_ADDSUB_ ## suba) << 21) | \
    ((RDP_COMB_ALPHA_ADDSUB_ ## subb) <<  3) | \
    ((RDP_COMB_ALPHA_MUL_    ## mul)  << 18) | \
    ((RDP_COMB_ALPHA_ADDSUB_ ## add)  <<  0)

#define RdpSetCombiner_2C(rgb1, alpha1, rgb2, alpha2) \
    ((cast64(0x3C)<<56) | \
    RDP__Comb1_Rgb rgb1 | \
    RDP__Comb2_Rgb rgb2 | \
    RDP__Comb1_Alpha alpha1 | \
    RDP__Comb2_Alpha alpha2)

#define RdpSetCombiner_1C(rgb, alpha) \
    RdpSetCombiner_2C(rgb, alpha, rgb, alpha)

/*************************************************************************
 * Render modes
 *************************************************************************/

#define SOM_ATOMIC             (cast64(1)<<55)

#define SOM_CYCLE_1            ((cast64(0))<<52)
#define SOM_CYCLE_2            ((cast64(1))<<52)
#define SOM_CYCLE_COPY         ((cast64(2))<<52)
#define SOM_CYCLE_FILL         ((cast64(3))<<52)
#define SOM_CYCLE_MASK         ((cast64(3))<<52)

#define SOM_TEXTURE_DETAIL     (cast64(1)<<50)
#define SOM_TEXTURE_SHARPEN    (cast64(1)<<49)

#define SOM_ENABLE_TLUT_RGB16  (cast64(2)<<46)
#define SOM_ENABLE_TLUT_I88    (cast64(3)<<46)

#define SOM_SAMPLE_1X1         (cast64(0)<<45)
#define SOM_SAMPLE_2X2         (cast64(1)<<45)
#define SOM_MIDTEXEL           (cast64(1)<<44)

#define SOM_TC_FILTER          (cast64(0)<<41)  // NOTE: this values are bit-inverted, so that they end up with a good default
#define SOM_TC_FILTERCONV      (cast64(3)<<41)
#define SOM_TC_CONV            (cast64(6)<<41)

#define SOM_RGBDITHER_SQUARE   ((cast64(0))<<38)
#define SOM_RGBDITHER_BAYER    ((cast64(1))<<38)
#define SOM_RGBDITHER_NOISE    ((cast64(2))<<38)
#define SOM_RGBDITHER_NONE     ((cast64(3))<<38)

#define SOM_ALPHADITHER_SQUARE ((cast64(0))<<36)
#define SOM_ALPHADITHER_BAYER  ((cast64(1))<<36)
#define SOM_ALPHADITHER_NOISE  ((cast64(2))<<36)
#define SOM_ALPHADITHER_NONE   ((cast64(3))<<36)

#define SOM_BLENDING           (cast64(1)<<14)
#define SOM_FBREAD             (cast64(1)<<6)
#define SOM_Z_WRITE            (cast64(1)<<5)
#define SOM_Z_COMPARE          (cast64(1)<<4)
#define SOM_ALPHA_COMPARE      (cast64(1)<<0)

#define RDP__BL_PM_PIXEL_RGB    cast64(0)
#define RDP__BL_PM_MEM_RGB      cast64(1)
#define RDP__BL_PM_BLEND_RGB    cast64(2)
#define RDP__BL_PM_FOG_RGB      cast64(3)

#define RDP__BL_A_PIXEL_ALPHA   cast64(0)
#define RDP__BL_A_FOG_ALPHA     cast64(1)
#define RDP__BL_A_SHADE_ALPHA   cast64(2)
#define RDP__BL_A_ZERO          cast64(3)

#define RDP__BL_B_ONEMA         cast64(0)
#define RDP__BL_B_MEM_ALPHA     cast64(1)
#define RDP__BL_B_ONE           cast64(2)
#define RDP__BL_B_ZERO          cast64(3)

#define RDP__BL1(p,a,m,b) \
    ((RDP__BL_PM_ ## p) << 30) | \
    ((RDP__BL_A_  ## a) << 26) | \
    ((RDP__BL_PM_ ## m) << 22) | \
    ((RDP__BL_B_  ## b) << 18)

#define RDP__BL2(p,a,m,b) \
    ((RDP__BL_PM_ ## p) << 28) | \
    ((RDP__BL_A_  ## a) << 24) | \
    ((RDP__BL_PM_ ## m) << 20) | \
    ((RDP__BL_B_  ## b) << 16)

#define SOM_BLENDER_2C(c1, c2)   ((RDP__BL1 c1) | (RDP__BL2 c2))
#define SOM_BLENDER_1C(c)        SOM_BLENDER_2C(c,c)

#define RdpSetOtherModes(som_flags) \
    ((cast64(0x2f)<<56) | ((som_flags) ^ (cast64(6)<<41)))


/**********************************************************
 * Mid-level macros
 **********************************************************/

#define RDP_AUTO_TMEM_SLOT(n)   (-(n))
#define RDP_AUTO_PITCH          (-1)

#define RDP_NUM_SLOTS_TILE4BPP(w, h)   (0x800 / ((w)*(h)/2))
#define RDP_NUM_SLOTS_PALETTE16        16

/**
 * MRdpLoadTex4bpp - Display list for loading a 4bpp texture into TMEM
 *
 * @param tidx          Tile ID (0-7)
 * @param rdram_addr    Address of the texture in RDRAM
 * @param width         Width of the texture in pixels
 * @param height        Height of the texture in pixels
 * @param pitch         Pitch of the texture in RDRAM in bytes, 
 *                      or RDP_AUTO_PITCH in case the texture is linear in memory.
 * @param tmem_addr     Address of TMEM where to load the texture,
 *                      or RDP_AUTO_TMEM_SLOT(n) to load the texture in the Nth
 *                      available slot for textures of this size.
 * @param tmem_pitch    Pitch of the texture in TMEM in bytes,
 *                      or RDP_AUTO_PITCH to store the texture linearly.
 *
 * @note RDP_AUTO_TMEM_SLOT(n) allow to allocate TMEM using slots of fixed size.
 *       The slot size is calculated given the texture width / height. You can
 *       use RDP_NUM_SLOTS_TILE4BPP to calculate how many slots are available
 *       for a given texture size. If you need to load textures of different
 *       sizes, RDP_AUTO_TMEM_SLOT cannot be used, and TMEM addresses must
 *       be calculated manually.
 */
#ifndef __ASSEMBLER__
    #define MRdpLoadTex4bpp(tidx, rdram_addr, width, height, pitch, tmem_addr, tmem_pitch) \
        RdpSetTile(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_8BIT, (tmem_pitch) < 0 ? (width)/8 : tmem_pitch/8, (tmem_addr) < 0 ? -(tmem_addr) * (width)*(height)/2/8 : tmem_addr, tidx), \
        RdpSetTexImage(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_8BIT, rdram_addr, (pitch) < 0 ? (width)/2 : (pitch)), \
        RdpLoadTileI(tidx, 0, 0, (width)/2, (height))
#else
    #define MRdpLoadTex4bpp_Slot_Autopitch(tidx, rdram_addr, width, height, tmem_addr) \
        RdpSetTile(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_8BIT, (width)/8, -(tmem_addr) * (width)*(height)/2/8, tidx), \
        RdpSetTexImage(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_8BIT, rdram_addr, (width)/2), \
        RdpLoadTileI(tidx, 0, 0, (width)/2, (height))
#endif

/**
 * MRdpLoadPalette16 - Display list for loading a 16-color palette into TMEM
 *
 * @param tid           Tile ID (0-7)
 * @param rdram_addr    Address of the palette in RDRAM
 * @param tmem_addr     Address of the palette in TMEM,
 *                      or RDP_AUTO_TMEM_SLOT(n) to load the palette into the Nth
 *                      available slot for palettes of 16 colors.
 *
 * @note The maximum number of 16-bit palettes that can be stored in TMEM is
 * RDRDP_NUM_SLOTS_PALETTE16 (16).
 *
 */
#ifndef __ASSEMBLER__
    #define MRdpLoadPalette16(tidx, rdram_addr, tmem_addr) \
        RdpSetTile(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_4BIT, 16, ((tmem_addr) <= 0 ? (0x800 + -(tmem_addr)*(16*2*4)) : tmem_addr)/8, tidx), \
        RdpSetTexImage(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_16BIT, rdram_addr, 16), \
        RdpLoadTlut(tidx, 0, 15)
#else
    #define MRdpLoadPalette16_Addr(tidx, rdram_addr, tmem_addr) \
        RdpSetTile(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_4BIT, 16, tmem_addr/8, tidx), \
        RdpSetTexImage(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_16BIT, rdram_addr, 16), \
        RdpLoadTlut(tidx, 0, 15)
    #define MRdpLoadPalette16_Slot(tidx, rdram_addr, slot) \
        RdpSetTile(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_4BIT, 16, (0x800 + -(slot)*(16*2*4))/8, tidx), \
        RdpSetTexImage(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_16BIT, rdram_addr, 16), \
        RdpLoadTlut(tidx, 0, 15)
#endif


/**
 * MRdpSetTile4bpp - Display list for configure a tile ID to draw a 4bpp texture
 *
 * @param tidx             Tile ID (0-7)
 * @param tmem_tex_addr    Address in TMEM of the texture, or RDP_AUTO_TMEM_SLOT
 *                         to select the nth slot for textures of this size.
 * @param tmem_tex_pitch   Pitch in TMEM of the texture in bytes, or RDP_AUTO_PITCH
 *                         if the texture is stored linearly.
 * @param tmem_pal_addr    Address in TMEM of the palette, or RDP_AUTO_TMEM_SLOT
 *                         to select the nth available palette.
 * @param width            Width of the texture in pixels
 * @param height           Height of the texture in pixels
 *
 * @note You can load TMEM using MRdpLoadTile4bpp and MRdpLoadPalette16.
 */

#ifndef __ASSEMBLER__
    #define MRdpSetTile4bpp(tidx, tmem_tex_addr, tmem_tex_pitch, tmem_pal_addr, width, height) \
        RdpSetTile(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_4BIT, \
            (tmem_tex_pitch) < 0 ? (width)/8 : tmem_tex_pitch, \
            (tmem_tex_addr) < 0 ? -(tmem_tex_addr) * (width)*(height)/2/8 : tmem_tex_addr, tidx) \
            | (((tmem_pal_addr)<0 ? -(tmem_pal_addr) : ((tmem_pal_addr)&0x780)>>7) << 20), \
        RdpSetTileSizeI(tidx, 0, 0, (width)-1, (height)-1)
#else
    #define MRdpSetTile4bpp_Slot_Autopitch(tidx, tmem_tex_addr, tmem_pal_addr, width, height) \
        RdpSetTile(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_4BIT, \
            (width)/8, \
            -(tmem_tex_addr) * (width)*(height)/2/8, tidx) \
            | ((-(tmem_pal_addr)) << 20), \
        RdpSetTileSizeI(tidx, 0, 0, (width)-1, (height)-1)
#endif

/**
 * MRdpDrawRect4bpp - Display list for drawing a 4bpp textured rectangle
 *
 * @param tidx             Tile ID (0-7) previously setup using MRdpSetTile4bpp
 * @param x                X coordinate of the rectangle
 * @param y                Y coordinate of the rectangle
 * @param w                width of the rectangle
 * @param h                height of the rectangle
 *
 */

#define MRdpTextureRectangle4bpp(tidx, x, y, w, h) \
    RdpTextureRectangle1I(tidx, x, y, (x)+(w)-1, (y)+(h)-1), \
    RdpTextureRectangle2I(0, 0, 4, 1)


#endif
