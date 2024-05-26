#ifndef __LIBDRAGON_MAGMA_H
#define __LIBDRAGON_MAGMA_H

#include <stdbool.h>
#include <stdint.h>
#include <graphics.h>
#include <rsp.h>
#include <rspq.h>
#include <debug.h>

/** @brief An instance of the magma pipeline, with an attached vertex shader */
typedef struct mg_pipeline_s        mg_pipeline_t;

/** @brief A linear array of data, which can be bound to a pipeline for various purposes */
typedef struct mg_buffer_s          mg_buffer_t;

/** @brief A set of resources, that can be bound for use by a shader */
typedef struct mg_resource_set_s    mg_resource_set_t;

typedef enum
{
    MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST     = 0,
    MG_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP    = 1,
    MG_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN      = 2,
} mg_primitive_topology_t;

typedef enum
{
    MG_GEOMETRY_FLAGS_Z_ENABLED             = 0x1,
    MG_GEOMETRY_FLAGS_TEX_ENABLED           = 0x2,
    MG_GEOMETRY_FLAGS_SHADE_ENABLED         = 0x4,
} mg_geometry_flags_t;

typedef enum
{
    MG_CULL_MODE_NONE                       = 0,
    MG_CULL_MODE_BACK                       = 1,
    MG_CULL_MODE_FRONT                      = 2,
} mg_cull_mode_t;

typedef enum
{
    MG_FRONT_FACE_COUNTER_CLOCKWISE         = 0,
    MG_FRONT_FACE_CLOCKWISE                 = 1,
} mg_front_face_t;

// TODO: rethink these flags
typedef enum
{
    MG_BUFFER_FLAGS_USAGE_VERTEX            = 0x1,
    MG_BUFFER_FLAGS_USAGE_INDEX             = 0x2,
    MG_BUFFER_FLAGS_USAGE_UNIFORM           = 0x4,
    MG_BUFFER_FLAGS_LAZY_ALLOC              = 0x8,
} mg_buffer_flags_t;

typedef enum
{
    MG_BUFFER_MAP_FLAGS_READ                = 0x1,
    MG_BUFFER_MAP_FLAGS_WRITE               = 0x2,
} mg_buffer_map_flags_t;

typedef enum
{
    MG_RESOURCE_TYPE_UNIFORM_BUFFER         = 0,
    MG_RESOURCE_TYPE_STORAGE_BUFFER         = 1,
    MG_RESOURCE_TYPE_INLINE_UNIFORM         = 2,
} mg_resource_type_t;

typedef struct
{
    mg_cull_mode_t cull_mode;
    mg_front_face_t front_face;
} mg_culling_parms_t;

typedef struct
{
    float x;
    float y;
    float width;
    float height;
    float minDepth;
    float maxDepth;
} mg_viewport_t;

typedef struct
{
    uint32_t binding;
    uint32_t offset;
    uint32_t size;
} mg_uniform_t;

typedef struct
{
    rsp_ucode_t *vertex_shader_ucode;
    uint32_t uniform_count;
    mg_uniform_t *uniforms;
} mg_pipeline_parms_t;

typedef struct
{
    mg_primitive_topology_t primitive_topology;
    bool primitive_restart_enabled;
} mg_input_assembly_parms_t;

typedef struct
{
    mg_buffer_flags_t flags;
    const void *initial_data;
    uint32_t size;
} mg_buffer_parms_t;

typedef struct
{
    uint32_t binding;
    mg_resource_type_t type;
    mg_buffer_t *buffer;
    const void *inline_data;
    uint32_t offset;
} mg_resource_binding_t;

typedef struct
{
    mg_pipeline_t *pipeline;
    uint32_t binding_count;
    mg_resource_binding_t *bindings;
} mg_resource_set_parms_t;

#ifdef __cplusplus
extern "C" {
#endif

void mg_init(void);
void mg_close(void);

// NOTE: The following functions are not commands, so they are not automatically synchronized with RSP!

/* Pipelines */

mg_pipeline_t *mg_pipeline_create(mg_pipeline_parms_t *parms);
void mg_pipeline_free(mg_pipeline_t *pipeline);

/* Buffers */

mg_buffer_t *mg_buffer_create(mg_buffer_parms_t *parms);
void mg_buffer_free(mg_buffer_t *buffer);
void *mg_buffer_map(mg_buffer_t *buffer, uint32_t offset, uint32_t size, mg_buffer_map_flags_t flags);
void mg_buffer_unmap(mg_buffer_t *buffer);
void mg_buffer_write(mg_buffer_t *buffer, uint32_t offset, uint32_t size, const void *data);

/* Resources */

mg_resource_set_t *mg_resource_set_create(mg_resource_set_parms_t *parms);
void mg_resource_set_free(mg_resource_set_t *resource_set);


/* Commands (these will generate rspq commands) */

/** @brief Bind the pipeline for subsequent use, uploading the attached shader to IMEM */
void mg_bind_pipeline(mg_pipeline_t *pipeline);

/** @brief Set culling flags */
inline void mg_set_culling(mg_culling_parms_t *culling);

/** @brief Set culling flags */
inline void mg_set_geometry_flags(mg_geometry_flags_t flags);

/** @brief Set the viewport */
inline void mg_set_viewport(mg_viewport_t *viewport);

/** @brief Bind a resource set, uploading the bound resources to DMEM */
void mg_bind_resource_set(mg_resource_set_t *resource_set);

/** @brief Push a block of data directly to DMEM, embedding the data in the command */
void mg_push_constants(uint32_t offset, uint32_t size, const void *data);

/** @brief Bind a vertex buffer to be used by subsequent drawing commands */
void mg_bind_vertex_buffer(mg_buffer_t *buffer, uint32_t offset);

/** @brief Draw primitives */
void mg_draw(mg_input_assembly_parms_t *input_assembly_parms, uint32_t vertex_count, uint32_t first_vertex);

/** @brief Draw indexed primitives */
void mg_draw_indexed(mg_input_assembly_parms_t *input_assembly_parms, const uint16_t *indices, uint32_t index_count, int32_t vertex_offset);

// TODO: Instanced draw calls?
// TODO: Indirect draw calls?

/// @cond

/* Inline function definitions and prerequisites */

#define mg_cmd_write(cmd_id, ...)   rspq_write(mg_overlay_id, cmd_id, ##__VA_ARGS__)

extern uint32_t mg_overlay_id;

enum
{
    MG_CMD_SET_BYTE             = 0x0,
    MG_CMD_SET_SHORT            = 0x1,
    MG_CMD_SET_WORD             = 0x2,
    MG_CMD_SET_QUAD             = 0x3,
    MG_CMD_SET_SHADER           = 0x4,
    MG_CMD_LOAD_VERTICES        = 0x5,
    MG_CMD_DRAW_INDICES         = 0x6,
    MG_CMD_LOAD_UNIFORM         = 0x7,
    MG_CMD_PUSH_CONSTANT_8      = 0x8,
    MG_CMD_PUSH_CONSTANT_16     = 0x9,
    MG_CMD_PUSH_CONSTANT_32     = 0xA,
    MG_CMD_PUSH_CONSTANT_64     = 0xB,
    MG_CMD_PUSH_CONSTANT_128    = 0xC,
    MG_CMD_PUSH_CONSTANT_MAX    = 0xD,
};

typedef struct
{
    int16_t scale[4];
    int16_t offset[4];
} mg_rsp_viewport_t;

typedef struct
{
    mg_rsp_viewport_t viewport;
    uint16_t clip_factors[4];
    uint32_t shader_code;
    uint32_t shader_data;
    uint16_t shader_code_size;
    uint16_t shader_data_size;
    uint32_t vertex_buffer;
    uint16_t tri_cmd;
    uint8_t cull_mode;
} __attribute__((packed)) mg_rsp_state_t;

inline void mg_cmd_set_byte(uint32_t offset, uint8_t value)
{
    mg_cmd_write(MG_CMD_SET_BYTE, offset, value);
}

inline void mg_cmd_set_short(uint32_t offset, uint16_t value)
{
    mg_cmd_write(MG_CMD_SET_SHORT, offset, value);
}

inline void mg_cmd_set_word(uint32_t offset, uint32_t value)
{
    mg_cmd_write(MG_CMD_SET_WORD, offset, value);
}

inline void mg_cmd_set_quad(uint32_t offset, uint32_t value0, uint32_t value1, uint32_t value2, uint32_t value3)
{
    mg_cmd_write(MG_CMD_SET_QUAD, offset, value0, value1, value2, value3);
}

inline uint8_t mg_culling_parms_to_rsp_state(mg_culling_parms_t *culling)
{
    uint8_t cull_mode;
    uint8_t is_front_cw;

    switch (culling->cull_mode) {
    case MG_CULL_MODE_NONE:
        cull_mode = 2;
        break;
    case MG_CULL_MODE_BACK:
        cull_mode = 1;
        break;
    case MG_CULL_MODE_FRONT:
        cull_mode = 0;
        break;
    default:
        assertf(0, "%d is not a valid cull mode!", culling->cull_mode);
    }

    switch (culling->front_face) {
    case MG_FRONT_FACE_COUNTER_CLOCKWISE:
        is_front_cw = 0;
        break;
    case MG_FRONT_FACE_CLOCKWISE:
        is_front_cw = 1;
        break;
    default:
        assertf(0, "%d is not a valid front face winding direction!", culling->front_face);
    }

    // If the front face is clockwise, flip the cull mode. 
    // If the cull mode is NONE anyway, this has no effect because the final value is still > 1
    return cull_mode ^ is_front_cw;
}

inline mg_rsp_viewport_t mg_viewport_to_rsp_state(mg_viewport_t *viewport)
{
    float half_width = viewport->width / 2;
    float half_height = viewport->height / 2;
    float depth_diff = viewport->maxDepth - viewport->minDepth;
    float half_depth = depth_diff / 2;
    return (mg_rsp_viewport_t) {
        .scale = {
            half_width,
            half_height,
            half_depth,
            1.0f
        },
        .offset = {
            viewport->x - half_width,
            viewport->y - half_height,
            viewport->minDepth - half_depth,
            0.0f
        }
    };
}

inline void mg_set_culling(mg_culling_parms_t *culling)
{
    mg_cmd_set_byte(offsetof(mg_rsp_state_t, cull_mode), mg_culling_parms_to_rsp_state(culling));
}

inline void mg_set_geometry_flags(mg_geometry_flags_t flags)
{
    uint16_t tricmd = 0x8 | (flags&0x7);
    mg_cmd_set_short(offsetof(mg_rsp_state_t, tri_cmd), tricmd << 8);
}

inline void mg_set_viewport(mg_viewport_t *viewport)
{
    mg_rsp_viewport_t rsp_viewport = mg_viewport_to_rsp_state(viewport);
    uint32_t value0 = (rsp_viewport.scale[0] << 16) | rsp_viewport.scale[1];
    uint32_t value1 = (rsp_viewport.scale[2] << 16) | rsp_viewport.scale[3];
    uint32_t value2 = (rsp_viewport.offset[0] << 16) | rsp_viewport.offset[1];
    uint32_t value3 = (rsp_viewport.offset[2] << 16) | rsp_viewport.offset[3];
    mg_cmd_set_quad(offsetof(mg_rsp_state_t, viewport), value0, value1, value2, value3);
}
/// @endcond

#ifdef __cplusplus
}
#endif

#endif