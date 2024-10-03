#ifndef __LIBDRAGON_MGFX_H
#define __LIBDRAGON_MGFX_H

#include <magma.h>
#include <mgfx_constants.h>
#include <mgfx_macros.h>

/* Enums */

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

#ifdef __cplusplus
extern "C" {
#endif

/* Functions */

rsp_ucode_t *mgfx_get_shader_ucode();

/* Convert parameter structs to RSP side uniform structs */

void mgfx_get_fog(mgfx_fog_t *dst, const mgfx_fog_parms_t *parms);
void mgfx_get_lighting(mgfx_lighting_t *dst, const mgfx_lighting_parms_t *parms);
void mgfx_get_texturing(mgfx_texturing_t *dst, const mgfx_texturing_parms_t *parms);
void mgfx_get_modes(mgfx_modes_t *dst, const mgfx_modes_parms_t *parms);
void mgfx_get_matrices(mgfx_matrices_t *dst, const mgfx_matrices_parms_t *parms);

/* Set uniforms directly inline */

void mgfx_set_fog_inline(const mg_uniform_t *uniform, const mgfx_fog_parms_t *parms);
void mgfx_set_lighting_inline(const mg_uniform_t *uniform, const mgfx_lighting_parms_t *parms);
void mgfx_set_texturing_inline(const mg_uniform_t *uniform, const mgfx_texturing_parms_t *parms);
void mgfx_set_modes_inline(const mg_uniform_t *uniform, const mgfx_modes_parms_t *parms);
void mgfx_set_matrices_inline(const mg_uniform_t *uniform, const mgfx_matrices_parms_t *parms);

#ifdef __cplusplus
}
#endif

#endif