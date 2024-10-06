/**
 * @file mgfx_macros.h
 * @brief Helper macros for mgfx
 * @ingroup magma
 */

#ifndef __LIBDRAGON_MGFX_MACROS_H
#define __LIBDRAGON_MGFX_MACROS_H

/** @brief Convert a number to s10.5 format, which is used for vertex positions in mgfx. */
#define MGFX_S10_5(v)       ((v)*(1<<5))

/** @brief Convert a number to s8.8 format, which may be used for texture coordinates in mgfx (Though any signed fixed point format is accepted). */
#define MGFX_S8_8(v)        ((v)*(1<<8))

/** @brief Construct a vertex position in a format supported by the mgfx shader. */
#define MGFX_POS(x, y, z)   { MGFX_S10_5(x), MGFX_S10_5(y), MGFX_S10_5(z) }

/** @brief Construct a texture coordinate in a format supported by the mgfx shader. */
#define MGFX_TEX(s, t)      { MGFX_S8_8(s), MGFX_S8_8(t) }

/** @brief Construct a vertex normal in a format supported by the mgfx shader. */
#define MGFX_NRM(x, y, z)   ((((x) & 0x1F)<<11) | \
                             (((y) & 0x3F)<<5)  | \
                             (((z) & 0x1F)<<0))

#endif
