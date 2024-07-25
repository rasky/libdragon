#ifndef __LIBDRAGON_MGFX_H
#define __LIBDRAGON_MGFX_H

#include <magma.h>
#include <mgfx_constants.h>

/* Enums */

typedef enum 
{
    // Position is always enabled
    MGFX_VTX_LAYOUT_NORMAL    = (1<<0),
    MGFX_VTX_LAYOUT_COLOR     = (1<<1),
    MGFX_VTX_LAYOUT_TEXCOORDS = (1<<2)
} mgfx_vtx_layout_t;

typedef enum
{
    MGFX_MODES_FLAGS_FOG_ENABLED        = MGFX_FLAG_FOG,
    MGFX_MODES_FLAGS_ENV_MAP_ENABLED    = MGFX_FLAG_ENV_MAP,
} mgfx_modes_flags_t;

/* RSP side uniform structs */

typedef struct
{
    int16_t factor_int;
    int16_t offset_int;
    uint16_t factor_frac;
    uint16_t offset_frac;
} __attribute__((packed, aligned(16))) mgfx_fog_t;

typedef struct
{
    int16_t position[4];
    int16_t color[4];
    int16_t attenuation_int[4];
    uint16_t attenuation_frac[4];
} __attribute__((packed, aligned(16))) mgfx_light_t;

typedef struct
{
    mgfx_light_t lights[MGFX_LIGHT_COUNT_MAX];
    int16_t ambient[4];
    uint32_t count;
} __attribute__((packed, aligned(16))) mgfx_lighting_t;

typedef struct
{
    int16_t tex_scale[2];
    int16_t tex_offset[2];
} __attribute__((packed, aligned(16))) mgfx_texturing_t;

typedef struct
{
    uint32_t flags;
} __attribute__((packed, aligned(16))) mgfx_modes_t;

typedef struct
{
    int16_t  i[16];
    uint16_t f[16];
} __attribute__((packed, aligned(16))) mgfx_matrix_t;

typedef struct
{
    mgfx_matrix_t mvp;
    mgfx_matrix_t mv;
    mgfx_matrix_t normal;
} __attribute__((packed, aligned(16))) mgfx_matrices_t;

/* Parameter structs */

typedef struct
{
    mgfx_vtx_layout_t vtx_layout;
} mgfx_pipeline_parms_t;

typedef struct
{
    float start;
    float end;
} mgfx_fog_parms_t;

typedef struct
{
    float position[4];
    color_t color;
    float radius;
} mgfx_light_parms_t;

typedef struct
{
    color_t ambient_color;
    const mgfx_light_parms_t *lights;
    uint32_t light_count;
} mgfx_lighting_parms_t;

typedef struct
{
    int16_t scale[2];
    int16_t offset[2];
} mgfx_texturing_parms_t;

typedef struct
{
    mgfx_modes_flags_t flags;
} mgfx_modes_parms_t;

typedef struct
{
    const float *model_view_projection;
    const float *model_view;
    const float *normal;
} mgfx_matrices_parms_t;

/* Vertex struct */

#define MGFX_S10_5(v)       ((v)*(1<<5))
#define MGFX_S8_8(v)        ((v)*(1<<8))

#define MGFX_POS(x, y, z)   { MGFX_S10_5(x), MGFX_S10_5(y), MGFX_S10_5(z) }
#define MGFX_TEX(s, t)      { MGFX_S8_8(s), MGFX_S8_8(t) }
#define MGFX_NRM(x, y, z)   ((((x) & 0x1F)<<11) | \
                             (((y) & 0x3F)<<5)  | \
                             (((z) & 0x1F)<<0))

#ifdef __cplusplus
extern "C" {
#endif

/* Functions */

mg_pipeline_t *mgfx_create_pipeline(const mgfx_pipeline_parms_t *parms);

/* Convert parameter structs to RSP side uniform structs */

void mgfx_get_fog(mgfx_fog_t *dst, const mgfx_fog_parms_t *parms);
void mgfx_get_lighting(mgfx_lighting_t *dst, const mgfx_lighting_parms_t *parms);
void mgfx_get_texturing(mgfx_texturing_t *dst, const mgfx_texturing_parms_t *parms);
void mgfx_get_modes(mgfx_modes_t *dst, const mgfx_modes_parms_t *parms);
void mgfx_get_matrices(mgfx_matrices_t *dst, const mgfx_matrices_parms_t *parms);

/* Set uniforms directly inline */

void mgfx_set_fog_inline(const mgfx_fog_parms_t *parms);
void mgfx_set_lighting_inline(const mgfx_lighting_parms_t *parms);
void mgfx_set_texturing_inline(const mgfx_texturing_parms_t *parms);
void mgfx_set_modes_inline(const mgfx_modes_parms_t *parms);
void mgfx_set_matrices_inline(const mgfx_matrices_parms_t *parms);

#ifdef __cplusplus
}
#endif

#endif
