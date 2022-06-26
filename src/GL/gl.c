#include "GL/gl.h"
#include "rdpq.h"
#include "rspq.h"
#include "display.h"
#include "rdp.h"
#include "utils.h"
#include <string.h>
#include <math.h>

#define MODELVIEW_STACK_SIZE  32
#define PROJECTION_STACK_SIZE 2

#define CLIPPING_PLANE_COUNT  6
#define CLIPPING_CACHE_SIZE   9

#define LIGHT_COUNT           8

#define RADIANS(x) ((x) * M_PI / 180.0f)

#define CLAMP(x, min, max) (MIN(MAX((x), (min)), (max)))
#define CLAMP01(x) CLAMP((x), 0, 1)

#define CLAMPF_TO_BOOL(x)  ((x)!=0.0)

#define CLAMPF_TO_U8(x)  ((x)*0xFF)
#define CLAMPF_TO_I8(x)  ((x)*0x7F)
#define CLAMPF_TO_U16(x) ((x)*0xFFFF)
#define CLAMPF_TO_I16(x) ((x)*0x7FFF)
#define CLAMPF_TO_U32(x) ((x)*0xFFFFFFFF)
#define CLAMPF_TO_I32(x) ((x)*0x7FFFFFFF)

#define FLOAT_TO_U8(x)  (CLAMP((x), 0.f, 1.f)*0xFF)

#define U8_TO_FLOAT(x) ((x)/(float)(0xFF))
#define U16_TO_FLOAT(x) ((x)/(float)(0xFFFF))
#define U32_TO_FLOAT(x) ((x)/(float)(0xFFFFFFFF))
#define I8_TO_FLOAT(x) MAX((x)/(float)(0x7F),-1.f)
#define I16_TO_FLOAT(x) MAX((x)/(float)(0x7FFF),-1.f)
#define I32_TO_FLOAT(x) MAX((x)/(float)(0x7FFFFFFF),-1.f)


typedef struct {
    surface_t *color_buffer;
    void *depth_buffer;
} gl_framebuffer_t;

typedef struct {
    GLfloat position[4];
    GLfloat screen_pos[2];
    GLfloat color[4];
    GLfloat texcoord[2];
    GLfloat inverse_w;
    GLfloat depth;
    uint8_t clip;
} gl_vertex_t;

typedef struct {
    GLfloat m[4][4];
} gl_matrix_t;

typedef struct {
    GLfloat scale[3];
    GLfloat offset[3];
} gl_viewport_t;

typedef struct {
    gl_matrix_t *storage;
    int32_t size;
    int32_t cur_depth;
} gl_matrix_stack_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    GLenum internal_format;
    GLenum format;
    GLenum type;
    GLenum wrap_s;
    GLenum wrap_t;
    GLenum min_filter;
    GLenum mag_filter;
    GLclampf border_color[4];
    GLclampf priority;
    void *data;
    bool is_dirty;
} gl_texture_object_t;

typedef struct {
    gl_vertex_t *vertices[CLIPPING_PLANE_COUNT + 3];
    uint32_t count;
} gl_clipping_list_t;

typedef struct {
    GLfloat ambient[4];
    GLfloat diffuse[4];
    GLfloat specular[4];
    GLfloat emissive[4];
    GLfloat shininess;
    GLenum color_target;
} gl_material_t;

typedef struct {
    GLfloat ambient[4];
    GLfloat diffuse[4];
    GLfloat specular[4];
    GLfloat position[4];
    GLfloat direction[3];
    GLfloat spot_exponent;
    GLfloat spot_cutoff;
    GLfloat constant_attenuation;
    GLfloat linear_attenuation;
    GLfloat quadratic_attenuation;
    bool enabled;
} gl_light_t;

static struct {
    gl_framebuffer_t default_framebuffer;
    gl_framebuffer_t *cur_framebuffer;

    GLenum current_error;

    GLenum draw_buffer;

    GLenum immediate_mode;

    GLclampf clear_color[4];
    GLclampd clear_depth;

    uint32_t scissor_box[4];

    bool cull_face;
    GLenum cull_face_mode;
    GLenum front_face;

    GLenum blend_src;
    GLenum blend_dst;

    GLenum depth_func;

    bool scissor_test;
    bool depth_test;
    bool texture_2d;
    bool blend;
    bool lighting;
    bool color_material;

    gl_vertex_t vertex_cache[3];
    uint32_t triangle_indices[3];
    uint32_t next_vertex;
    uint32_t triangle_progress;
    uint32_t triangle_counter;

    GLfloat current_color[4];
    GLfloat current_texcoord[4];
    GLfloat current_normal[3];

    gl_viewport_t current_viewport;

    GLenum matrix_mode;
    gl_matrix_t final_matrix;
    gl_matrix_t *current_matrix;

    gl_matrix_t modelview_stack_storage[MODELVIEW_STACK_SIZE];
    gl_matrix_t projection_stack_storage[PROJECTION_STACK_SIZE];

    gl_matrix_stack_t modelview_stack;
    gl_matrix_stack_t projection_stack;
    gl_matrix_stack_t *current_matrix_stack;

    gl_texture_object_t texture_2d_object;

    gl_material_t materials[2];
    gl_light_t lights[LIGHT_COUNT];

    GLfloat light_model_ambient[4];
    bool light_model_local_viewer;
    bool light_model_two_side;

    GLenum shade_model;

    bool is_scissor_dirty;
} state;

static const float clip_planes[CLIPPING_PLANE_COUNT][4] = {
    { 1, 0, 0, 1 },
    { 0, 1, 0, 1 },
    { 0, 0, 1, 1 },
    { 1, 0, 0, -1 },
    { 0, 1, 0, -1 },
    { 0, 0, 1, -1 },
};

#define assert_framebuffer() ({ \
    assertf(state.cur_framebuffer != NULL, "GL: No target is set!"); \
})

void gl_set_framebuffer(gl_framebuffer_t *framebuffer)
{
    state.cur_framebuffer = framebuffer;
    glViewport(0, 0, framebuffer->color_buffer->width, framebuffer->color_buffer->height);
    rdpq_set_color_image_surface_no_scissor(state.cur_framebuffer->color_buffer);
    rdpq_set_z_image(state.cur_framebuffer->depth_buffer);
}

void gl_set_default_framebuffer()
{
    surface_t *ctx;
    while (!(ctx = display_lock()));

    gl_framebuffer_t *fb = &state.default_framebuffer;

    if (fb->depth_buffer != NULL && (fb->color_buffer == NULL 
                                    || fb->color_buffer->width != ctx->width 
                                    || fb->color_buffer->height != ctx->height)) {
        free_uncached(fb->depth_buffer);
        fb->depth_buffer = NULL;
    }

    fb->color_buffer = ctx;

    // TODO: only allocate depth buffer if depth test is enabled? Lazily allocate?
    if (fb->depth_buffer == NULL) {
        // TODO: allocate in separate RDRAM bank?
        fb->depth_buffer = malloc_uncached_aligned(64, ctx->width * ctx->height * 2);
    }

    gl_set_framebuffer(fb);
}

gl_matrix_t * gl_matrix_stack_get_matrix(gl_matrix_stack_t *stack)
{
    return &stack->storage[stack->cur_depth];
}

void gl_update_current_matrix()
{
    state.current_matrix = gl_matrix_stack_get_matrix(state.current_matrix_stack);
}

void gl_matrix_mult(GLfloat *d, const gl_matrix_t *m, const GLfloat *v)
{
    d[0] = m->m[0][0] * v[0] + m->m[1][0] * v[1] + m->m[2][0] * v[2] + m->m[3][0] * v[3];
    d[1] = m->m[0][1] * v[0] + m->m[1][1] * v[1] + m->m[2][1] * v[2] + m->m[3][1] * v[3];
    d[2] = m->m[0][2] * v[0] + m->m[1][2] * v[1] + m->m[2][2] * v[2] + m->m[3][2] * v[3];
    d[3] = m->m[0][3] * v[0] + m->m[1][3] * v[1] + m->m[2][3] * v[2] + m->m[3][3] * v[3];
}

void gl_matrix_mult3x3(GLfloat *d, const gl_matrix_t *m, const GLfloat *v)
{
    d[0] = m->m[0][0] * v[0] + m->m[1][0] * v[1] + m->m[2][0] * v[2];
    d[1] = m->m[0][1] * v[0] + m->m[1][1] * v[1] + m->m[2][1] * v[2];
    d[2] = m->m[0][2] * v[0] + m->m[1][2] * v[1] + m->m[2][2] * v[2];
}

void gl_matrix_mult_full(gl_matrix_t *d, const gl_matrix_t *l, const gl_matrix_t *r)
{
    gl_matrix_mult(d->m[0], l, r->m[0]);
    gl_matrix_mult(d->m[1], l, r->m[1]);
    gl_matrix_mult(d->m[2], l, r->m[2]);
    gl_matrix_mult(d->m[3], l, r->m[3]);
}

void gl_update_final_matrix()
{
    gl_matrix_mult_full(&state.final_matrix, gl_matrix_stack_get_matrix(&state.projection_stack), gl_matrix_stack_get_matrix(&state.modelview_stack));
}

void gl_init_material(gl_material_t *material)
{
    *material = (gl_material_t) {
        .ambient = { 0.2f, 0.2f, 0.2f, 1.0f },
        .diffuse = { 0.8f, 0.8f, 0.8f, 1.0f },
        .specular = { 0.0f, 0.0f, 0.0f, 1.0f },
        .emissive = { 0.0f, 0.0f, 0.0f, 1.0f },
        .shininess = 0.0f,
        .color_target = GL_AMBIENT_AND_DIFFUSE,
    };
}

void gl_init_light(gl_light_t *light)
{
    *light = (gl_light_t) {
        .ambient = { 0.0f, 0.0f, 0.0f, 1.0f },
        .diffuse = { 0.0f, 0.0f, 0.0f, 1.0f },
        .specular = { 0.0f, 0.0f, 0.0f, 1.0f },
        .position = { 0.0f, 0.0f, 1.0f, 0.0f },
        .direction = { 0.0f, 0.0f, -1.0f },
        .spot_exponent = 0.0f,
        .spot_cutoff = 180.0f,
        .constant_attenuation = 1.0f,
        .linear_attenuation = 0.0f,
        .quadratic_attenuation = 0.0f,
        .enabled = false,
    };
}

void gl_init()
{
    rdpq_init();

    memset(&state, 0, sizeof(state));

    state.modelview_stack = (gl_matrix_stack_t) {
        .storage = state.modelview_stack_storage,
        .size = MODELVIEW_STACK_SIZE,
    };

    state.projection_stack = (gl_matrix_stack_t) {
        .storage = state.projection_stack_storage,
        .size = PROJECTION_STACK_SIZE,
    };

    state.texture_2d_object = (gl_texture_object_t) {
        .wrap_s = GL_REPEAT,
        .wrap_t = GL_REPEAT,
        .min_filter = GL_NEAREST_MIPMAP_LINEAR,
        .mag_filter = GL_LINEAR,
    };

    gl_init_material(&state.materials[0]);
    gl_init_material(&state.materials[1]);

    for (uint32_t i = 0; i < LIGHT_COUNT; i++)
    {
        gl_init_light(&state.lights[i]);
    }

    state.lights[0].diffuse[0] = 0.2f;
    state.lights[0].diffuse[1] = 0.2f;
    state.lights[0].diffuse[2] = 0.2f;

    state.lights[0].specular[0] = 0.8f;
    state.lights[0].specular[1] = 0.8f;
    state.lights[0].specular[2] = 0.8f;

    state.light_model_ambient[0] = 0.2f;
    state.light_model_ambient[1] = 0.2f;
    state.light_model_ambient[2] = 0.2f;
    state.light_model_ambient[3] = 1.0f;
    state.light_model_local_viewer = false;
    state.light_model_two_side = false;

    glDrawBuffer(GL_FRONT);
    glDepthRange(0, 1);
    glClearDepth(1.0);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glBlendFunc(GL_ONE, GL_ZERO);
    glDepthFunc(GL_LESS);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    rdpq_set_other_modes_raw(0);
    gl_set_default_framebuffer();

    glScissor(0, 0, state.cur_framebuffer->color_buffer->width, state.cur_framebuffer->color_buffer->height);
}

void gl_close()
{
    rdpq_close();
}

GLenum glGetError(void)
{
    GLenum error = state.current_error;
    state.current_error = GL_NO_ERROR;
    return error;
}

void gl_set_error(GLenum error)
{
    state.current_error = error;
    assert(error);
}

void gl_swap_buffers()
{
    rdpq_sync_full((void(*)(void*))display_show, state.default_framebuffer.color_buffer);
    rspq_flush();
    gl_set_default_framebuffer();
}

void gl_set_flag(GLenum target, bool value)
{
    switch (target) {
    case GL_SCISSOR_TEST:
        state.is_scissor_dirty = value != state.scissor_test;
        state.scissor_test = value;
        break;
    case GL_CULL_FACE:
        state.cull_face = value;
        break;
    case GL_DEPTH_TEST:
        state.depth_test = value;
        break;
    case GL_TEXTURE_2D:
        state.texture_2d = value;
        break;
    case GL_BLEND:
        state.blend = value;
        break;
    case GL_LIGHTING:
        state.lighting = value;
        break;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
    case GL_LIGHT4:
    case GL_LIGHT5:
    case GL_LIGHT6:
    case GL_LIGHT7:
        state.lights[target - GL_LIGHT0].enabled = value;
        break;
    case GL_COLOR_MATERIAL:
        state.color_material = value;
        break;
    case GL_COLOR_LOGIC_OP:
    case GL_INDEX_LOGIC_OP:
        assertf(!value, "Logical pixel operation is not supported!");
        break;
    case GL_LINE_STIPPLE:
    case GL_POLYGON_STIPPLE:
        assertf(!value, "Stipple is not supported!");
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glEnable(GLenum target)
{
    gl_set_flag(target, true);
}

void glDisable(GLenum target)
{
    gl_set_flag(target, false);
}

tex_format_t gl_texture_get_format(const gl_texture_object_t *texture_object)
{
    switch (texture_object->internal_format) {
    case GL_RGB5_A1:
        return FMT_RGBA16;
    case GL_RGBA8:
        return FMT_RGBA32;
    case GL_LUMINANCE4_ALPHA4:
        return FMT_IA8;
    case GL_LUMINANCE8_ALPHA8:
        return FMT_IA16;
    case GL_LUMINANCE8:
    case GL_INTENSITY8:
        return FMT_I8;
    default:
        return FMT_NONE;
    }
}

uint32_t gl_log2(uint32_t s)
{
    uint32_t log = 0;
    while (s >>= 1) ++log;
    return log;
}

bool gl_is_invisible()
{
    return state.draw_buffer == GL_NONE 
        || (state.depth_test && state.depth_func == GL_NEVER);
}

void gl_apply_scissor()
{
    if (!state.is_scissor_dirty) {
        return;
    }

    uint32_t w = state.cur_framebuffer->color_buffer->width;
    uint32_t h = state.cur_framebuffer->color_buffer->height;

    if (state.scissor_test) {
        rdpq_set_scissor(
            state.scissor_box[0],
            h - state.scissor_box[1] - state.scissor_box[3],
            state.scissor_box[0] + state.scissor_box[2],
            h - state.scissor_box[1]
        );
    } else {
        rdpq_set_scissor(0, 0, w, h);
    }
}

void glBegin(GLenum mode)
{
    if (state.immediate_mode) {
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    switch (mode) {
    case GL_TRIANGLES:
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        state.immediate_mode = mode;
        state.next_vertex = 0;
        state.triangle_progress = 0;
        state.triangle_counter = 0;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    if (gl_is_invisible()) {
        return;
    }

    gl_apply_scissor();

    uint64_t modes = SOM_CYCLE_1;

    if (0 /* antialiasing */) {
        modes |= SOM_AA_ENABLE | SOM_READ_ENABLE | SOM_COLOR_ON_COVERAGE | SOM_COVERAGE_DEST_CLAMP | SOM_ALPHA_USE_CVG;
    }

    if (state.depth_test) {
        modes |= SOM_Z_WRITE | SOM_Z_OPAQUE | SOM_Z_SOURCE_PIXEL;

        if (state.depth_func == GL_LESS) {
            modes |= SOM_Z_COMPARE;
        }
    }

    rdpq_combiner_t comb;
    if (state.texture_2d) {
        modes |= SOM_TEXTURE_PERSP | SOM_TC_FILTER;

        tex_format_t fmt = gl_texture_get_format(&state.texture_2d_object);

        gl_texture_object_t *tex_obj = &state.texture_2d_object;

        if (tex_obj->mag_filter == GL_LINEAR) {
            modes |= SOM_SAMPLE_2X2;
        }

        comb = RDPQ_COMBINER1((TEX0, ZERO, SHADE, ZERO), (TEX0, ZERO, SHADE, ZERO));

        if (tex_obj->is_dirty) {
            // TODO: min filter (mip mapping?)
            // TODO: border color?
            rdpq_set_texture_image(tex_obj->data, fmt, tex_obj->width);

            uint8_t mask_s = tex_obj->wrap_s == GL_REPEAT ? gl_log2(tex_obj->width) : 0;
            uint8_t mask_t = tex_obj->wrap_t == GL_REPEAT ? gl_log2(tex_obj->height) : 0;

            rdpq_set_tile_full(0, fmt, 0, tex_obj->width * TEX_FORMAT_BYTES_PER_PIXEL(fmt), 0, 0, 0, mask_t, 0, 0, 0, mask_s, 0);
            rdpq_load_tile(0, 0, 0, tex_obj->width, tex_obj->height);
            tex_obj->is_dirty = false;
        }
    } else {
        comb = RDPQ_COMBINER1((ONE, ZERO, SHADE, ZERO), (ONE, ZERO, SHADE, ZERO));
    }

    rdpq_set_other_modes_raw(modes);
    rdpq_mode_combiner(comb);

    if (state.blend) {
        // TODO: derive the blender config from blend_src and blend_dst
        rdpq_mode_blender(RDPQ_BLENDER1((PIXEL_RGB, MUX_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
    }
}

void glEnd(void)
{
    if (!state.immediate_mode) {
        gl_set_error(GL_INVALID_OPERATION);
    }

    state.immediate_mode = 0;
}

void gl_draw_triangle(gl_vertex_t *v0, gl_vertex_t *v1, gl_vertex_t *v2)
{
    if (state.cull_face_mode == GL_FRONT_AND_BACK) {
        return;
    }

    if (state.cull_face)
    {
        float winding = v0->screen_pos[0] * (v1->screen_pos[1] - v2->screen_pos[1]) +
                        v1->screen_pos[0] * (v2->screen_pos[1] - v0->screen_pos[1]) +
                        v2->screen_pos[0] * (v0->screen_pos[1] - v1->screen_pos[1]);
        
        bool is_front = (state.front_face == GL_CCW) ^ (winding > 0.0f);
        GLenum face = is_front ? GL_FRONT : GL_BACK;

        if (state.cull_face_mode == face) {
            return;
        }
    }

    int32_t tex_offset = state.texture_2d ? 6 : -1;
    int32_t z_offset = state.depth_test ? 9 : -1;

    rdpq_triangle(0, 0, 0, 2, tex_offset, z_offset, v0->screen_pos, v1->screen_pos, v2->screen_pos);
}

static float dot_product3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float dot_product4(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

void gl_vertex_calc_screenspace(gl_vertex_t *v)
{
    float inverse_w = 1.0f / v->position[3];

    v->screen_pos[0] = v->position[0] * inverse_w * state.current_viewport.scale[0] + state.current_viewport.offset[0];
    v->screen_pos[1] = v->position[1] * inverse_w * state.current_viewport.scale[1] + state.current_viewport.offset[1];

    v->depth = v->position[2] * inverse_w * state.current_viewport.scale[2] + state.current_viewport.offset[2];

    v->inverse_w = inverse_w;

    v->clip = 0;
    for (uint32_t i = 0; i < 3; i++)
    {
        if (v->position[i] < - v->position[3]) {
            v->clip |= 1 << i;
        } else if (v->position[i] > v->position[3]) {
            v->clip |= 1 << (i + 3);
        }
    }
}

void gl_clip_triangle(gl_vertex_t *v0, gl_vertex_t *v1, gl_vertex_t *v2)
{
    if (v0->clip & v1->clip & v2->clip) {
        return;
    }

    uint8_t any_clip = v0->clip | v1->clip | v2->clip;

    if (!any_clip) {
        gl_draw_triangle(v0, v1, v2);
        return;
    }

    // Polygon clipping using the Sutherland-Hodgman algorithm
    // See https://en.wikipedia.org/wiki/Sutherland%E2%80%93Hodgman_algorithm

    // Intersection points are stored in the clipping cache
    gl_vertex_t clipping_cache[CLIPPING_CACHE_SIZE];
    uint32_t cache_used = 0;

    gl_clipping_list_t lists[2];

    gl_clipping_list_t *in_list = &lists[0];
    gl_clipping_list_t *out_list = &lists[1];

    out_list->vertices[0] = v0;
    out_list->vertices[1] = v1;
    out_list->vertices[2] = v2;
    out_list->count = 3;
    
    for (uint32_t c = 0; c < CLIPPING_PLANE_COUNT; c++)
    {
        // If nothing clips this plane, skip it entirely
        if ((any_clip & (1<<c)) == 0) {
            continue;
        }

        const float *clip_plane = clip_planes[c];

        SWAP(in_list, out_list);
        out_list->count = 0;

        uint32_t cache_unused = 0;

        for (uint32_t i = 0; i < in_list->count; i++)
        {
            gl_vertex_t *cur_point = in_list->vertices[i];
            gl_vertex_t *prev_point = in_list->vertices[(i + in_list->count - 1) % in_list->count];

            bool cur_inside = (cur_point->clip & (1<<c)) == 0;
            bool prev_inside = (prev_point->clip & (1<<c)) == 0;

            if (cur_inside ^ prev_inside) {
                gl_vertex_t *intersection = NULL;

                for (uint32_t n = 0; n < CLIPPING_CACHE_SIZE; n++)
                {
                    if ((cache_used & (1<<n)) == 0) {
                        intersection = &clipping_cache[n];
                        cache_used |= (1<<n);
                        break;
                    }
                }

                assertf(intersection, "clipping cache full!");
                assertf(intersection != cur_point, "invalid intersection");
                assertf(intersection != prev_point, "invalid intersection");

                float d0 = dot_product4(prev_point->position, clip_plane);
                float d1 = dot_product4(cur_point->position, clip_plane);
                
                float a = d0 / (d0 - d1);

                assertf(a >= 0.f && a <= 1.f, "invalid a: %f", a);

                intersection->position[0] = lerp(prev_point->position[0], cur_point->position[0], a);
                intersection->position[1] = lerp(prev_point->position[1], cur_point->position[1], a);
                intersection->position[2] = lerp(prev_point->position[2], cur_point->position[2], a);
                intersection->position[3] = lerp(prev_point->position[3], cur_point->position[3], a);

                gl_vertex_calc_screenspace(intersection);

                intersection->color[0] = lerp(prev_point->color[0], cur_point->color[0], a);
                intersection->color[1] = lerp(prev_point->color[1], cur_point->color[1], a);
                intersection->color[2] = lerp(prev_point->color[2], cur_point->color[2], a);
                intersection->color[3] = lerp(prev_point->color[3], cur_point->color[3], a);

                intersection->texcoord[0] = lerp(prev_point->texcoord[0], cur_point->texcoord[0], a);
                intersection->texcoord[1] = lerp(prev_point->texcoord[1], cur_point->texcoord[1], a);

                out_list->vertices[out_list->count++] = intersection;
            }

            if (cur_inside) {
                out_list->vertices[out_list->count++] = cur_point;
            } else {
                // If the point is in the clipping cache, remember it as unused
                uint32_t diff = cur_point - clipping_cache;
                if (diff >= 0 && diff < CLIPPING_CACHE_SIZE) {
                    cache_unused |= (1<<diff);
                }
            }
        }

        // Mark all points that were discarded as unused
        cache_used &= ~cache_unused;
    }

    for (uint32_t i = 2; i < out_list->count; i++)
    {
        gl_draw_triangle(out_list->vertices[0], out_list->vertices[i-1], out_list->vertices[i]);
    }
}

void gl_vertex_cache_changed()
{
    if (state.triangle_progress < 3) {
        return;
    }

    gl_vertex_t *v0 = &state.vertex_cache[state.triangle_indices[0]];
    gl_vertex_t *v1 = &state.vertex_cache[state.triangle_indices[1]];
    gl_vertex_t *v2 = &state.vertex_cache[state.triangle_indices[2]];

    switch (state.immediate_mode) {
    case GL_TRIANGLES:
        state.triangle_progress = 0;
        break;
    case GL_TRIANGLE_STRIP:
        state.triangle_progress = 2;
        state.triangle_indices[state.triangle_counter % 2] = state.triangle_indices[2];
        break;
    case GL_TRIANGLE_FAN:
        state.triangle_progress = 2;
        state.triangle_indices[1] = state.triangle_indices[2];
        break;
    }

    state.triangle_counter++;

    // Flat shading
    if (state.shade_model == GL_FLAT) {
        v0->color[0] = v1->color[0] = v2->color[0];
        v0->color[1] = v1->color[1] = v2->color[1];
        v0->color[2] = v1->color[2] = v2->color[2];
        v0->color[3] = v1->color[3] = v2->color[3];
    }

    gl_clip_triangle(v0, v1, v2);
}

float gl_mag2(const GLfloat *v)
{
    return v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
}

float gl_mag(const GLfloat *v)
{
    return sqrtf(gl_mag2(v));
}

void gl_normalize(GLfloat *d, const GLfloat *v)
{
    float inv_mag = 1.0f / gl_mag(v);

    d[0] = v[0] * inv_mag;
    d[1] = v[1] * inv_mag;
    d[2] = v[2] * inv_mag;
}

void gl_homogeneous_unit_diff(GLfloat *d, const GLfloat *p1, const GLfloat *p2)
{
    bool p1wzero = p1[3] == 0.0f;
    bool p2wzero = p2[3] == 0.0f;

    if (!(p1wzero ^ p2wzero)) {
        d[0] = p2[0] - p1[0];
        d[1] = p2[1] - p1[1];
        d[2] = p2[2] - p1[2];
    } else if (p1wzero) {
        d[0] = -p1[0];
        d[1] = -p1[1];
        d[2] = -p1[2];
    } else {
        d[0] = p2[0];
        d[1] = p2[1];
        d[2] = p2[2];
    }

    gl_normalize(d, d);
}

float gl_clamped_dot(const GLfloat *a, const GLfloat *b)
{
    return MAX(dot_product3(a, b), 0.0f);
}

const GLfloat * gl_material_get_color(const gl_material_t *material, GLenum color)
{
    GLenum target = material->color_target;

    switch (color) {
    case GL_EMISSION:
        return state.color_material && target == GL_EMISSION ? state.current_color : material->emissive;
    case GL_AMBIENT:
        return state.color_material && (target == GL_AMBIENT || target == GL_AMBIENT_AND_DIFFUSE) ? state.current_color : material->ambient;
    case GL_DIFFUSE:
        return state.color_material && (target == GL_DIFFUSE || target == GL_AMBIENT_AND_DIFFUSE) ? state.current_color : material->diffuse;
    case GL_SPECULAR:
        return state.color_material && target == GL_SPECULAR ? state.current_color : material->specular;
    default:
        assertf(0, "Invalid material color!");
        return NULL;
    }
}

void gl_perform_lighting(GLfloat *color, const GLfloat *position, const gl_material_t *material)
{
    const GLfloat *emissive = gl_material_get_color(material, GL_EMISSION);
    const GLfloat *ambient = gl_material_get_color(material, GL_AMBIENT);
    const GLfloat *diffuse = gl_material_get_color(material, GL_DIFFUSE);
    const GLfloat *specular = gl_material_get_color(material, GL_SPECULAR);

    // Emission and ambient
    color[0] = emissive[0] + ambient[0] * state.light_model_ambient[0];
    color[1] = emissive[1] + ambient[1] * state.light_model_ambient[1];
    color[2] = emissive[2] + ambient[2] * state.light_model_ambient[2];
    color[3] = diffuse[3];

    const gl_matrix_t *mv = gl_matrix_stack_get_matrix(&state.modelview_stack);

    GLfloat v[4];
    gl_matrix_mult(v, mv, position);

    GLfloat n[3];
    gl_matrix_mult3x3(n, mv, state.current_normal);

    for (uint32_t l = 0; l < LIGHT_COUNT; l++)
    {
        const gl_light_t *light = &state.lights[l];
        if (!light->enabled) {
            continue;
        }

        // Spotlight
        float spot = 1.0f;
        if (light->spot_cutoff != 180.0f) {
            GLfloat plv[3];
            gl_homogeneous_unit_diff(plv, light->position, v);

            GLfloat s[3];
            gl_normalize(s, light->direction);

            float plvds = gl_clamped_dot(plv, s);

            if (plvds < cosf(RADIANS(light->spot_cutoff))) {
                // Outside of spotlight cutoff
                continue;
            }

            spot = powf(plvds, light->spot_exponent);
        }

        // Attenuation
        float att = 1.0f;
        if (light->position[3] != 0.0f) {
            GLfloat diff[3] = {
                v[0] - light->position[0],
                v[1] - light->position[1],
                v[2] - light->position[2],
            };
            float dsq = gl_mag2(diff);
            float d = sqrtf(dsq);
            att = 1.0f / (light->constant_attenuation + light->linear_attenuation * d + light->quadratic_attenuation * dsq);
        }

        // Light ambient color
        GLfloat col[3] = {
            ambient[1] * light->ambient[1],
            ambient[0] * light->ambient[0],
            ambient[2] * light->ambient[2],
        };

        GLfloat vpl[3];
        gl_homogeneous_unit_diff(vpl, v, light->position);

        float ndvp = gl_clamped_dot(n, vpl);

        // Diffuse
        col[0] += diffuse[0] * light->diffuse[0] * ndvp;
        col[1] += diffuse[1] * light->diffuse[1] * ndvp;
        col[2] += diffuse[2] * light->diffuse[2] * ndvp;

        // Specular
        if (ndvp != 0.0f) {
            GLfloat h[3] = {
                vpl[0],
                vpl[1],
                vpl[2],
            };
            if (state.light_model_local_viewer) {
                GLfloat pe[4] = { 0, 0, 0, 1 };
                gl_homogeneous_unit_diff(pe, v, pe);
                h[0] += pe[0];
                h[1] += pe[1];
                h[2] += pe[2];
            } else {
                h[2] += 1;
            }
            gl_normalize(h, h);
            
            float ndh = gl_clamped_dot(n, h);
            float spec_factor = powf(ndh, material->shininess);

            col[0] += specular[0] * light->specular[0] * spec_factor;
            col[1] += specular[1] * light->specular[1] * spec_factor;
            col[2] += specular[2] * light->specular[2] * spec_factor;
        }

        float light_factor = att * spot;

        color[0] += col[0] * light_factor;
        color[1] += col[1] * light_factor;
        color[2] += col[2] * light_factor;
    }
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) 
{
    if (gl_is_invisible()) {
        return;
    }

    gl_vertex_t *v = &state.vertex_cache[state.next_vertex];

    GLfloat tmp[] = {x, y, z, w};

    if (state.lighting) {
        // TODO: Back face material?
        gl_perform_lighting(v->color, tmp, &state.materials[0]);
    } else {
        v->color[0] = state.current_color[0];
        v->color[1] = state.current_color[1];
        v->color[2] = state.current_color[2];
        v->color[3] = state.current_color[3];
    }

    v->color[0] = CLAMP01(v->color[0]) * 255.f;
    v->color[1] = CLAMP01(v->color[1]) * 255.f;
    v->color[2] = CLAMP01(v->color[2]) * 255.f;
    v->color[3] = CLAMP01(v->color[3]) * 255.f;

    gl_matrix_mult(v->position, &state.final_matrix, tmp);
    gl_vertex_calc_screenspace(v);

    if (state.texture_2d) {
        v->texcoord[0] = state.current_texcoord[0] * state.texture_2d_object.width;
        v->texcoord[1] = state.current_texcoord[1] * state.texture_2d_object.height;

        if (state.texture_2d_object.mag_filter == GL_LINEAR) {
            v->texcoord[0] -= 0.5f;
            v->texcoord[1] -= 0.5f;
        }

        v->texcoord[0] *= 32.f;
        v->texcoord[1] *= 32.f;
    }

    state.triangle_indices[state.triangle_progress] = state.next_vertex;

    state.next_vertex = (state.next_vertex + 1) % 3;
    state.triangle_progress++;

    gl_vertex_cache_changed();
}

void glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w)     { glVertex4f(x, y, z, w); }
void glVertex4i(GLint x, GLint y, GLint z, GLint w)             { glVertex4f(x, y, z, w); }
void glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { glVertex4f(x, y, z, w); }

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)    { glVertex4f(x, y, z, 1); }
void glVertex3s(GLshort x, GLshort y, GLshort z)    { glVertex3f(x, y, z); }
void glVertex3i(GLint x, GLint y, GLint z)          { glVertex3f(x, y, z); }
void glVertex3d(GLdouble x, GLdouble y, GLdouble z) { glVertex3f(x, y, z); }

void glVertex2f(GLfloat x, GLfloat y)   { glVertex4f(x, y, 0, 1); }
void glVertex2s(GLshort x, GLshort y)   { glVertex2f(x, y); }
void glVertex2i(GLint x, GLint y)       { glVertex2f(x, y); }
void glVertex2d(GLdouble x, GLdouble y) { glVertex2f(x, y); }

void glVertex2sv(const GLshort *v)  { glVertex2s(v[0], v[1]); }
void glVertex2iv(const GLint *v)    { glVertex2i(v[0], v[1]); }
void glVertex2fv(const GLfloat *v)  { glVertex2f(v[0], v[1]); }
void glVertex2dv(const GLdouble *v) { glVertex2d(v[0], v[1]); }

void glVertex3sv(const GLshort *v)  { glVertex3s(v[0], v[1], v[2]); }
void glVertex3iv(const GLint *v)    { glVertex3i(v[0], v[1], v[2]); }
void glVertex3fv(const GLfloat *v)  { glVertex3f(v[0], v[1], v[2]); }
void glVertex3dv(const GLdouble *v) { glVertex3d(v[0], v[1], v[2]); }

void glVertex4sv(const GLshort *v)  { glVertex4s(v[0], v[1], v[2], v[3]); }
void glVertex4iv(const GLint *v)    { glVertex4i(v[0], v[1], v[2], v[3]); }
void glVertex4fv(const GLfloat *v)  { glVertex4f(v[0], v[1], v[2], v[3]); }
void glVertex4dv(const GLdouble *v) { glVertex4d(v[0], v[1], v[2], v[3]); }

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    state.current_color[0] = r;
    state.current_color[1] = g;
    state.current_color[2] = b;
    state.current_color[3] = a;
}

void glColor4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a)  { glColor4f(r, g, b, a); }
void glColor4b(GLbyte r, GLbyte g, GLbyte b, GLbyte a)          { glColor4f(I8_TO_FLOAT(r), I8_TO_FLOAT(g), I8_TO_FLOAT(b), I8_TO_FLOAT(a)); }
void glColor4s(GLshort r, GLshort g, GLshort b, GLshort a)      { glColor4f(I16_TO_FLOAT(r), I16_TO_FLOAT(g), I16_TO_FLOAT(b), I16_TO_FLOAT(a)); }
void glColor4i(GLint r, GLint g, GLint b, GLint a)              { glColor4f(I32_TO_FLOAT(r), I32_TO_FLOAT(g), I32_TO_FLOAT(b), I32_TO_FLOAT(a)); }
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)     { glColor4f(U8_TO_FLOAT(r), U8_TO_FLOAT(g), U8_TO_FLOAT(b), U8_TO_FLOAT(a)); }
void glColor4us(GLushort r, GLushort g, GLushort b, GLushort a) { glColor4f(U16_TO_FLOAT(r), U16_TO_FLOAT(g), U16_TO_FLOAT(b), U16_TO_FLOAT(a)); }
void glColor4ui(GLuint r, GLuint g, GLuint b, GLuint a)         { glColor4f(U32_TO_FLOAT(r), U32_TO_FLOAT(g), U32_TO_FLOAT(b), U32_TO_FLOAT(a)); }

void glColor3f(GLfloat r, GLfloat g, GLfloat b)     { glColor4f(r, g, b, 1.f); }
void glColor3d(GLdouble r, GLdouble g, GLdouble b)  { glColor3f(r, g, b); }
void glColor3b(GLbyte r, GLbyte g, GLbyte b)        { glColor3f(I8_TO_FLOAT(r), I8_TO_FLOAT(g), I8_TO_FLOAT(b)); }
void glColor3s(GLshort r, GLshort g, GLshort b)     { glColor3f(I16_TO_FLOAT(r), I16_TO_FLOAT(g), I16_TO_FLOAT(b)); }
void glColor3i(GLint r, GLint g, GLint b)           { glColor3f(I32_TO_FLOAT(r), I32_TO_FLOAT(g), I32_TO_FLOAT(b)); }
void glColor3ub(GLubyte r, GLubyte g, GLubyte b)    { glColor3f(U8_TO_FLOAT(r), U8_TO_FLOAT(g), U8_TO_FLOAT(b)); }
void glColor3us(GLushort r, GLushort g, GLushort b) { glColor3f(U16_TO_FLOAT(r), U16_TO_FLOAT(g), U16_TO_FLOAT(b)); }
void glColor3ui(GLuint r, GLuint g, GLuint b)       { glColor3f(U32_TO_FLOAT(r), U32_TO_FLOAT(g), U32_TO_FLOAT(b)); }

void glColor3bv(const GLbyte *v)    { glColor3b(v[0], v[1], v[2]); }
void glColor3sv(const GLshort *v)   { glColor3s(v[0], v[1], v[2]); }
void glColor3iv(const GLint *v)     { glColor3i(v[0], v[1], v[2]); }
void glColor3fv(const GLfloat *v)   { glColor3f(v[0], v[1], v[2]); }
void glColor3dv(const GLdouble *v)  { glColor3d(v[0], v[1], v[2]); }
void glColor3ubv(const GLubyte *v)  { glColor3ub(v[0], v[1], v[2]); }
void glColor3usv(const GLushort *v) { glColor3us(v[0], v[1], v[2]); }
void glColor3uiv(const GLuint *v)   { glColor3ui(v[0], v[1], v[2]); }

void glColor4bv(const GLbyte *v)    { glColor4b(v[0], v[1], v[2], v[3]); }
void glColor4sv(const GLshort *v)   { glColor4s(v[0], v[1], v[2], v[3]); }
void glColor4iv(const GLint *v)     { glColor4i(v[0], v[1], v[2], v[3]); }
void glColor4fv(const GLfloat *v)   { glColor4f(v[0], v[1], v[2], v[3]); }
void glColor4dv(const GLdouble *v)  { glColor4d(v[0], v[1], v[2], v[3]); }
void glColor4ubv(const GLubyte *v)  { glColor4ub(v[0], v[1], v[2], v[3]); }
void glColor4usv(const GLushort *v) { glColor4us(v[0], v[1], v[2], v[3]); }
void glColor4uiv(const GLuint *v)   { glColor4ui(v[0], v[1], v[2], v[3]); }

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    state.current_texcoord[0] = s;
    state.current_texcoord[1] = t;
    state.current_texcoord[2] = r;
    state.current_texcoord[3] = q;
}

void glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q)       { glTexCoord4f(s, t, r, q); }
void glTexCoord4i(GLint s, GLint t, GLint r, GLint q)               { glTexCoord4f(s, t, r, q); }
void glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q)   { glTexCoord4f(s, t, r, q); }

void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r)      { glTexCoord4f(s, t, r, 1.0f); }
void glTexCoord3s(GLshort s, GLshort t, GLshort r)      { glTexCoord3f(s, t, r); }
void glTexCoord3i(GLint s, GLint t, GLint r)            { glTexCoord3f(s, t, r); }
void glTexCoord3d(GLdouble s, GLdouble t, GLdouble r)   { glTexCoord3f(s, t, r); }

void glTexCoord2f(GLfloat s, GLfloat t)     { glTexCoord4f(s, t, 0.0f, 1.0f); }
void glTexCoord2s(GLshort s, GLshort t)     { glTexCoord2f(s, t); }
void glTexCoord2i(GLint s, GLint t)         { glTexCoord2f(s, t); }
void glTexCoord2d(GLdouble s, GLdouble t)   { glTexCoord2f(s, t); }

void glTexCoord1f(GLfloat s)    { glTexCoord4f(s, 0.0f, 0.0f, 1.0f); }
void glTexCoord1s(GLshort s)    { glTexCoord1f(s); }
void glTexCoord1i(GLint s)      { glTexCoord1f(s); }
void glTexCoord1d(GLdouble s)   { glTexCoord1f(s); }

void glTexCoord1sv(const GLshort *v)    { glTexCoord1s(v[0]); }
void glTexCoord1iv(const GLint *v)      { glTexCoord1i(v[0]); }
void glTexCoord1fv(const GLfloat *v)    { glTexCoord1f(v[0]); }
void glTexCoord1dv(const GLdouble *v)   { glTexCoord1d(v[0]); }

void glTexCoord2sv(const GLshort *v)    { glTexCoord2s(v[0], v[1]); }
void glTexCoord2iv(const GLint *v)      { glTexCoord2i(v[0], v[1]); }
void glTexCoord2fv(const GLfloat *v)    { glTexCoord2f(v[0], v[1]); }
void glTexCoord2dv(const GLdouble *v)   { glTexCoord2d(v[0], v[1]); }

void glTexCoord3sv(const GLshort *v)    { glTexCoord3s(v[0], v[1], v[2]); }
void glTexCoord3iv(const GLint *v)      { glTexCoord3i(v[0], v[1], v[2]); }
void glTexCoord3fv(const GLfloat *v)    { glTexCoord3f(v[0], v[1], v[2]); }
void glTexCoord3dv(const GLdouble *v)   { glTexCoord3d(v[0], v[1], v[2]); }

void glTexCoord4sv(const GLshort *v)    { glTexCoord4s(v[0], v[1], v[2], v[3]); }
void glTexCoord4iv(const GLint *v)      { glTexCoord4i(v[0], v[1], v[2], v[3]); }
void glTexCoord4fv(const GLfloat *v)    { glTexCoord4f(v[0], v[1], v[2], v[3]); }
void glTexCoord4dv(const GLdouble *v)   { glTexCoord4d(v[0], v[1], v[2], v[3]); }

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    state.current_normal[0] = nx;
    state.current_normal[1] = ny;
    state.current_normal[2] = nz;
}

void glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz)        { glNormal3f(I8_TO_FLOAT(nx), I8_TO_FLOAT(ny), I8_TO_FLOAT(nz)); }
void glNormal3s(GLshort nx, GLshort ny, GLshort nz)     { glNormal3f(I16_TO_FLOAT(nx), I16_TO_FLOAT(ny), I16_TO_FLOAT(nz)); }
void glNormal3i(GLint nx, GLint ny, GLint nz)           { glNormal3f(I32_TO_FLOAT(nx), I32_TO_FLOAT(ny), I32_TO_FLOAT(nz)); }
void glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz)  { glNormal3f(nx, ny, nz); }

void glNormal3bv(const GLbyte *v)   { glNormal3b(v[0], v[1], v[2]); }
void glNormal3sv(const GLshort *v)  { glNormal3s(v[0], v[1], v[2]); }
void glNormal3iv(const GLint *v)    { glNormal3i(v[0], v[1], v[2]); }
void glNormal3fv(const GLfloat *v)  { glNormal3f(v[0], v[1], v[2]); }
void glNormal3dv(const GLdouble *v) { glNormal3d(v[0], v[1], v[2]); }

void glDepthRange(GLclampd n, GLclampd f)
{
    state.current_viewport.scale[2] = ((f - n) * -0.5f) * 0x7FE0;
    state.current_viewport.offset[2] = (n + (f - n) * 0.5f) * 0x7FE0;
}

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    uint32_t fbh = state.cur_framebuffer->color_buffer->height;

    state.current_viewport.scale[0] = w * 0.5f;
    state.current_viewport.scale[1] = h * -0.5f;
    state.current_viewport.offset[0] = x + w * 0.5f;
    state.current_viewport.offset[1] = fbh - y - h * 0.5f;
}

void glMatrixMode(GLenum mode)
{
    switch (mode) {
    case GL_MODELVIEW:
        state.current_matrix_stack = &state.modelview_stack;
        break;
    case GL_PROJECTION:
        state.current_matrix_stack = &state.projection_stack;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    state.matrix_mode = mode;

    gl_update_current_matrix();
}

void glLoadMatrixf(const GLfloat *m)
{
    memcpy(state.current_matrix, m, sizeof(gl_matrix_t));
    gl_update_final_matrix();
}

void glLoadMatrixd(const GLdouble *m)
{
    for (size_t i = 0; i < 16; i++)
    {
        state.current_matrix->m[i/4][i%4] = m[i];
    }
    gl_update_final_matrix();
}

void glMultMatrixf(const GLfloat *m)
{
    gl_matrix_t tmp = *state.current_matrix;
    gl_matrix_mult_full(state.current_matrix, &tmp, (gl_matrix_t*)m);
    gl_update_final_matrix();
}

void glMultMatrixd(const GLdouble *m);

void glLoadIdentity(void)
{
    *state.current_matrix = (gl_matrix_t){ .m={
        {1,0,0,0},
        {0,1,0,0},
        {0,0,1,0},
        {0,0,0,1},
    }};

    gl_update_final_matrix();
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float c = cosf(angle);
    float s = sinf(angle);
    float ic = 1.f - c;

    float mag = sqrtf(x*x + y*y + z*z);
    x /= mag;
    y /= mag;
    z /= mag;

    gl_matrix_t rotation = (gl_matrix_t){ .m={
        {x*x*ic+c,   y*x*ic+z*s, z*x*ic-y*s, 0.f},
        {x*y*ic-z*s, y*y*ic+c,   z*y*ic+x*s, 0.f},
        {x*z*ic+y*s, y*z*ic-x*s, z*z*ic+c,   0.f},
        {0.f,        0.f,        0.f,        1.f},
    }};

    glMultMatrixf(rotation.m[0]);
}
void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    gl_matrix_t translation = (gl_matrix_t){ .m={
        {1.f, 0.f, 0.f, 0.f},
        {0.f, 1.f, 0.f, 0.f},
        {0.f, 0.f, 1.f, 0.f},
        {x,   y,   z,   1.f},
    }};

    glMultMatrixf(translation.m[0]);
}
void glTranslated(GLdouble x, GLdouble y, GLdouble z);

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    gl_matrix_t scale = (gl_matrix_t){ .m={
        {x,   0.f, 0.f, 0.f},
        {0.f, y,   0.f, 0.f},
        {0.f, 0.f, z,   0.f},
        {0.f, 0.f, 0.f, 1.f},
    }};

    glMultMatrixf(scale.m[0]);
}
void glScaled(GLdouble x, GLdouble y, GLdouble z);

void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    gl_matrix_t frustum = (gl_matrix_t){ .m={
        {(2*n)/(r-l), 0.f,           0.f,            0.f},
        {0.f,         (2.f*n)/(t-b), 0.f,            0.f},
        {(r+l)/(r-l), (t+b)/(t-b),   -(f+n)/(f-n),   -1.f},
        {0.f,         0.f,           -(2*f*n)/(f-n), 0.f},
    }};

    glMultMatrixf(frustum.m[0]);
}

void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    gl_matrix_t ortho = (gl_matrix_t){ .m={
        {2.0f/(r-l),   0.f,          0.f,          0.f},
        {0.f,          2.0f/(t-b),   0.f,          0.f},
        {0.f,          0.f,          2.0f/(f-n),   0.f},
        {-(r+l)/(r-l), -(t+b)/(t-b), -(f+n)/(f-n), 1.f},
    }};

    glMultMatrixf(ortho.m[0]);
}

void glPushMatrix(void)
{
    gl_matrix_stack_t *stack = state.current_matrix_stack;

    int32_t new_depth = stack->cur_depth + 1;
    if (new_depth >= stack->size) {
        gl_set_error(GL_STACK_OVERFLOW);
        return;
    }

    stack->cur_depth = new_depth;
    memcpy(&stack->storage[new_depth], &stack->storage[new_depth-1], sizeof(gl_matrix_t));

    gl_update_current_matrix();
}

void glPopMatrix(void)
{
    gl_matrix_stack_t *stack = state.current_matrix_stack;

    int32_t new_depth = stack->cur_depth - 1;
    if (new_depth < 0) {
        gl_set_error(GL_STACK_UNDERFLOW);
        return;
    }

    stack->cur_depth = new_depth;

    gl_update_current_matrix();
}

void gl_set_material_paramf(gl_material_t *material, GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_AMBIENT:
        material->ambient[0] = params[0];
        material->ambient[1] = params[1];
        material->ambient[2] = params[2];
        material->ambient[3] = params[3];
        break;
    case GL_DIFFUSE:
        material->diffuse[0] = params[0];
        material->diffuse[1] = params[1];
        material->diffuse[2] = params[2];
        material->diffuse[3] = params[3];
        break;
    case GL_AMBIENT_AND_DIFFUSE:
        material->ambient[0] = params[0];
        material->ambient[1] = params[1];
        material->ambient[2] = params[2];
        material->ambient[3] = params[3];
        material->diffuse[0] = params[0];
        material->diffuse[1] = params[1];
        material->diffuse[2] = params[2];
        material->diffuse[3] = params[3];
        break;
    case GL_SPECULAR:
        material->specular[0] = params[0];
        material->specular[1] = params[1];
        material->specular[2] = params[2];
        material->specular[3] = params[3];
        break;
    case GL_EMISSION:
        material->emissive[0] = params[0];
        material->emissive[1] = params[1];
        material->emissive[2] = params[2];
        material->emissive[3] = params[3];
        break;
    case GL_SHININESS:
        material->shininess = params[0];
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void gl_set_material_parami(gl_material_t *material, GLenum pname, const GLint *params)
{
    switch (pname) {
    case GL_AMBIENT:
        material->ambient[0] = I32_TO_FLOAT(params[0]);
        material->ambient[1] = I32_TO_FLOAT(params[1]);
        material->ambient[2] = I32_TO_FLOAT(params[2]);
        material->ambient[3] = I32_TO_FLOAT(params[3]);
        break;
    case GL_DIFFUSE:
        material->diffuse[0] = I32_TO_FLOAT(params[0]);
        material->diffuse[1] = I32_TO_FLOAT(params[1]);
        material->diffuse[2] = I32_TO_FLOAT(params[2]);
        material->diffuse[3] = I32_TO_FLOAT(params[3]);
        break;
    case GL_AMBIENT_AND_DIFFUSE:
        material->ambient[0] = I32_TO_FLOAT(params[0]);
        material->ambient[1] = I32_TO_FLOAT(params[1]);
        material->ambient[2] = I32_TO_FLOAT(params[2]);
        material->ambient[3] = I32_TO_FLOAT(params[3]);
        material->diffuse[0] = I32_TO_FLOAT(params[0]);
        material->diffuse[1] = I32_TO_FLOAT(params[1]);
        material->diffuse[2] = I32_TO_FLOAT(params[2]);
        material->diffuse[3] = I32_TO_FLOAT(params[3]);
        break;
    case GL_SPECULAR:
        material->specular[0] = I32_TO_FLOAT(params[0]);
        material->specular[1] = I32_TO_FLOAT(params[1]);
        material->specular[2] = I32_TO_FLOAT(params[2]);
        material->specular[3] = I32_TO_FLOAT(params[3]);
        break;
    case GL_EMISSION:
        material->emissive[0] = I32_TO_FLOAT(params[0]);
        material->emissive[1] = I32_TO_FLOAT(params[1]);
        material->emissive[2] = I32_TO_FLOAT(params[2]);
        material->emissive[3] = I32_TO_FLOAT(params[3]);
        break;
    case GL_SHININESS:
        material->shininess = params[0];
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    switch (pname) {
    case GL_SHININESS:
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (face) {
    case GL_FRONT:
        gl_set_material_paramf(&state.materials[0], pname, &param);
        break;
    case GL_BACK:
        gl_set_material_paramf(&state.materials[1], pname, &param);
        break;
    case GL_FRONT_AND_BACK:
        gl_set_material_paramf(&state.materials[0], pname, &param);
        gl_set_material_paramf(&state.materials[1], pname, &param);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glMateriali(GLenum face, GLenum pname, GLint param) { glMaterialf(face, pname, param); }

void glMaterialiv(GLenum face, GLenum pname, const GLint *params)
{
    switch (pname) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_AMBIENT_AND_DIFFUSE:
    case GL_SPECULAR:
    case GL_EMISSION:
    case GL_SHININESS:
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (face) {
    case GL_FRONT:
        gl_set_material_parami(&state.materials[0], pname, params);
        break;
    case GL_BACK:
        gl_set_material_parami(&state.materials[1], pname, params);
        break;
    case GL_FRONT_AND_BACK:
        gl_set_material_parami(&state.materials[0], pname, params);
        gl_set_material_parami(&state.materials[1], pname, params);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_AMBIENT_AND_DIFFUSE:
    case GL_SPECULAR:
    case GL_EMISSION:
    case GL_SHININESS:
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (face) {
    case GL_FRONT:
        gl_set_material_paramf(&state.materials[0], pname, params);
        break;
    case GL_BACK:
        gl_set_material_paramf(&state.materials[1], pname, params);
        break;
    case GL_FRONT_AND_BACK:
        gl_set_material_paramf(&state.materials[0], pname, params);
        gl_set_material_paramf(&state.materials[1], pname, params);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

gl_light_t * gl_get_light(GLenum light)
{
    if (light < GL_LIGHT0 || light > GL_LIGHT7) {
        gl_set_error(GL_INVALID_ENUM);
        return NULL;
    }

    return &state.lights[light - GL_LIGHT0];
}

void glLightf(GLenum light, GLenum pname, GLfloat param)
{
    gl_light_t *l = gl_get_light(light);
    if (l == NULL) {
        return;
    }

    switch (pname) {
    case GL_SPOT_EXPONENT:
        l->spot_exponent = param;
        break;
    case GL_SPOT_CUTOFF:
        l->spot_cutoff = param;
        break;
    case GL_CONSTANT_ATTENUATION:
        l->constant_attenuation = param;
        break;
    case GL_LINEAR_ATTENUATION:
        l->linear_attenuation = param;
        break;
    case GL_QUADRATIC_ATTENUATION:
        l->quadratic_attenuation = param;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glLighti(GLenum light, GLenum pname, GLint param) { glLightf(light, pname, param); }

void glLightiv(GLenum light, GLenum pname, const GLint *params)
{
    gl_light_t *l = gl_get_light(light);
    if (l == NULL) {
        return;
    }

    switch (pname) {
    case GL_AMBIENT:
        l->ambient[0] = I32_TO_FLOAT(params[0]);
        l->ambient[1] = I32_TO_FLOAT(params[1]);
        l->ambient[2] = I32_TO_FLOAT(params[2]);
        l->ambient[3] = I32_TO_FLOAT(params[3]);
        break;
    case GL_DIFFUSE:
        l->diffuse[0] = I32_TO_FLOAT(params[0]);
        l->diffuse[1] = I32_TO_FLOAT(params[1]);
        l->diffuse[2] = I32_TO_FLOAT(params[2]);
        l->diffuse[3] = I32_TO_FLOAT(params[3]);
        break;
    case GL_SPECULAR:
        l->specular[0] = I32_TO_FLOAT(params[0]);
        l->specular[1] = I32_TO_FLOAT(params[1]);
        l->specular[2] = I32_TO_FLOAT(params[2]);
        l->specular[3] = I32_TO_FLOAT(params[3]);
        break;
    case GL_POSITION:
        l->position[0] = params[0];
        l->position[1] = params[1];
        l->position[2] = params[2];
        l->position[3] = params[3];
        gl_matrix_mult(l->position, gl_matrix_stack_get_matrix(&state.modelview_stack), l->position);
        break;
    case GL_SPOT_DIRECTION:
        l->direction[0] = params[0];
        l->direction[1] = params[1];
        l->direction[2] = params[2];
        gl_matrix_mult3x3(l->direction, gl_matrix_stack_get_matrix(&state.modelview_stack), l->direction);
        break;
    case GL_SPOT_EXPONENT:
        l->spot_exponent = params[0];
        break;
    case GL_SPOT_CUTOFF:
        l->spot_cutoff = params[0];
        break;
    case GL_CONSTANT_ATTENUATION:
        l->constant_attenuation = params[0];
        break;
    case GL_LINEAR_ATTENUATION:
        l->linear_attenuation = params[0];
        break;
    case GL_QUADRATIC_ATTENUATION:
        l->quadratic_attenuation = params[0];
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    gl_light_t *l = gl_get_light(light);
    if (l == NULL) {
        return;
    }

    switch (pname) {
    case GL_AMBIENT:
        l->ambient[0] = params[0];
        l->ambient[1] = params[1];
        l->ambient[2] = params[2];
        l->ambient[3] = params[3];
        break;
    case GL_DIFFUSE:
        l->diffuse[0] = params[0];
        l->diffuse[1] = params[1];
        l->diffuse[2] = params[2];
        l->diffuse[3] = params[3];
        break;
    case GL_SPECULAR:
        l->specular[0] = params[0];
        l->specular[1] = params[1];
        l->specular[2] = params[2];
        l->specular[3] = params[3];
        break;
    case GL_POSITION:
        gl_matrix_mult(l->position, gl_matrix_stack_get_matrix(&state.modelview_stack), params);
        break;
    case GL_SPOT_DIRECTION:
        gl_matrix_mult3x3(l->direction, gl_matrix_stack_get_matrix(&state.modelview_stack), params);
        break;
    case GL_SPOT_EXPONENT:
        l->spot_exponent = params[0];
        break;
    case GL_SPOT_CUTOFF:
        l->spot_cutoff = params[0];
        break;
    case GL_CONSTANT_ATTENUATION:
        l->constant_attenuation = params[0];
        break;
    case GL_LINEAR_ATTENUATION:
        l->linear_attenuation = params[0];
        break;
    case GL_QUADRATIC_ATTENUATION:
        l->quadratic_attenuation = params[0];
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glLightModeli(GLenum pname, GLint param) 
{
    switch (pname) {
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
        state.light_model_local_viewer = param != 0;
        break;
    case GL_LIGHT_MODEL_TWO_SIDE:
        state.light_model_two_side = param != 0;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}
void glLightModelf(GLenum pname, GLfloat param) { glLightModeli(pname, param); }

void glLightModeliv(GLenum pname, const GLint *params)
{
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT:
        state.light_model_ambient[0] = I32_TO_FLOAT(params[0]);
        state.light_model_ambient[1] = I32_TO_FLOAT(params[1]);
        state.light_model_ambient[2] = I32_TO_FLOAT(params[2]);
        state.light_model_ambient[3] = I32_TO_FLOAT(params[3]);
        break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
        state.light_model_local_viewer = params[0] != 0;
        break;
    case GL_LIGHT_MODEL_TWO_SIDE:
        state.light_model_two_side = params[0] != 0;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}
void glLightModelfv(GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT:
        state.light_model_ambient[0] = params[0];
        state.light_model_ambient[1] = params[1];
        state.light_model_ambient[2] = params[2];
        state.light_model_ambient[3] = params[3];
        break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
        state.light_model_local_viewer = params[0] != 0;
        break;
    case GL_LIGHT_MODEL_TWO_SIDE:
        state.light_model_two_side = params[0] != 0;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glColorMaterial(GLenum face, GLenum mode)
{
    switch (face) {
    case GL_FRONT:
        state.materials[0].color_target = mode;
        break;
    case GL_BACK:
        state.materials[1].color_target = mode;
        break;
    case GL_FRONT_AND_BACK:
        state.materials[0].color_target = mode;
        state.materials[1].color_target = mode;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glShadeModel(GLenum mode)
{
    switch (mode) {
    case GL_FLAT:
    case GL_SMOOTH:
        state.shade_model = mode;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glCullFace(GLenum mode)
{
    switch (mode) {
    case GL_BACK:
    case GL_FRONT:
    case GL_FRONT_AND_BACK:
        state.cull_face_mode = mode;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glFrontFace(GLenum dir)
{
    switch (dir) {
    case GL_CW:
    case GL_CCW:
        state.front_face = dir;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

GLint gl_choose_internalformat(GLint requested)
{
    switch (requested) {
    case 1:
    case GL_LUMINANCE:
    case GL_LUMINANCE4:
    case GL_LUMINANCE8:
    case GL_LUMINANCE12:
    case GL_LUMINANCE16:
        return GL_LUMINANCE8;

    // TODO: is intensity semantically equivalent to alpha?
    case GL_ALPHA:
    case GL_ALPHA4:
    case GL_ALPHA8:
    case GL_ALPHA12:
    case GL_ALPHA16:
    case GL_INTENSITY:
    case GL_INTENSITY4:
    case GL_INTENSITY8:
    case GL_INTENSITY12:
    case GL_INTENSITY16:
        return GL_INTENSITY8;

    case 2:
    case GL_LUMINANCE4_ALPHA4:
    case GL_LUMINANCE6_ALPHA2:
        return GL_LUMINANCE4_ALPHA4;

    case GL_LUMINANCE_ALPHA:
    case GL_LUMINANCE8_ALPHA8:
    case GL_LUMINANCE12_ALPHA4:
    case GL_LUMINANCE12_ALPHA12:
    case GL_LUMINANCE16_ALPHA16:
        return GL_LUMINANCE8_ALPHA8;

    case 3:
    case 4:
    case GL_RGB:
    case GL_R3_G3_B2:
    case GL_RGB4:
    case GL_RGB5:
    case GL_RGBA:
    case GL_RGBA2:
    case GL_RGBA4:
    case GL_RGB5_A1:
        return GL_RGB5_A1;

    case GL_RGB8:
    case GL_RGB10:
    case GL_RGB12:
    case GL_RGB16:
    case GL_RGBA8:
    case GL_RGB10_A2:
    case GL_RGBA12:
    case GL_RGBA16:
        return GL_RGBA8;

    default:
        return -1;
    }
}

bool gl_copy_pixels(void *dst, const void *src, GLint dst_fmt, GLenum src_fmt, GLenum src_type)
{
    // TODO: Actually copy the pixels. Right now this function does nothing unless the 
    //       source format/type does not match the destination format directly, then it asserts.

    switch (dst_fmt) {
    case GL_RGB5_A1:
        if (src_fmt == GL_RGBA && src_type == GL_UNSIGNED_SHORT_5_5_5_1_EXT) {
            return true;
        }
        break;
    case GL_RGBA8:
        if (src_fmt == GL_RGBA && (src_type == GL_UNSIGNED_BYTE || src_type == GL_BYTE || src_type == GL_UNSIGNED_INT_8_8_8_8_EXT)) {
            return true;
        }
        break;
    case GL_LUMINANCE4_ALPHA4:
        break;
    case GL_LUMINANCE8_ALPHA8:
        if (src_fmt == GL_LUMINANCE_ALPHA && (src_type == GL_UNSIGNED_BYTE || src_type == GL_BYTE)) {
            return true;
        }
        break;
    case GL_LUMINANCE8:
    case GL_INTENSITY8:
        if (src_fmt == GL_LUMINANCE && (src_type == GL_UNSIGNED_BYTE || src_type == GL_BYTE)) {
            return true;
        }
        break;
    }

    assertf(0, "Pixel format conversion not yet implemented!");

    return false;
}

gl_texture_object_t * gl_get_texture_object(GLenum target)
{
    switch (target) {
    case GL_TEXTURE_2D:
        return &state.texture_2d_object;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return NULL;
    }
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data)
{
    gl_texture_object_t *obj = gl_get_texture_object(target);
    if (obj == NULL) {
        return;
    }

    GLint preferred_format = gl_choose_internalformat(internalformat);
    if (preferred_format < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }

    switch (format) {
    case GL_COLOR_INDEX:
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_RGB:
    case GL_RGBA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (type) {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
    case GL_BITMAP:
    case GL_UNSIGNED_SHORT:
    case GL_SHORT:
    case GL_UNSIGNED_INT:
    case GL_INT:
    case GL_UNSIGNED_BYTE_3_3_2_EXT:
    case GL_UNSIGNED_SHORT_4_4_4_4_EXT:
    case GL_UNSIGNED_SHORT_5_5_5_1_EXT:
    case GL_UNSIGNED_INT_8_8_8_8_EXT:
    case GL_UNSIGNED_INT_10_10_10_2_EXT:
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    obj->data = (void*)data;
    gl_copy_pixels(obj->data, data, preferred_format, format, type);

    obj->width = width;
    obj->height = height;
    obj->internal_format = preferred_format;
    obj->format = format;
    obj->type = type;
    obj->is_dirty = true;
}

void gl_texture_set_wrap_s(gl_texture_object_t *obj, GLenum param)
{
    switch (param) {
    case GL_CLAMP:
    case GL_REPEAT:
        obj->wrap_s = param;
        obj->is_dirty = true;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void gl_texture_set_wrap_t(gl_texture_object_t *obj, GLenum param)
{
    switch (param) {
    case GL_CLAMP:
    case GL_REPEAT:
        obj->wrap_t = param;
        obj->is_dirty = true;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void gl_texture_set_min_filter(gl_texture_object_t *obj, GLenum param)
{
    switch (param) {
    case GL_NEAREST:
    case GL_LINEAR:
    case GL_NEAREST_MIPMAP_NEAREST:
    case GL_LINEAR_MIPMAP_NEAREST:
    case GL_NEAREST_MIPMAP_LINEAR:
    case GL_LINEAR_MIPMAP_LINEAR:
        obj->min_filter = param;
        obj->is_dirty = true;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void gl_texture_set_mag_filter(gl_texture_object_t *obj, GLenum param)
{
    switch (param) {
    case GL_NEAREST:
    case GL_LINEAR:
        obj->mag_filter = param;
        obj->is_dirty = true;
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void gl_texture_set_border_color(gl_texture_object_t *obj, GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    obj->border_color[0] = CLAMP01(r);
    obj->border_color[1] = CLAMP01(g);
    obj->border_color[2] = CLAMP01(b);
    obj->border_color[3] = CLAMP01(a);
    obj->is_dirty = true;
}

void gl_texture_set_priority(gl_texture_object_t *obj, GLclampf param)
{
    obj->priority = CLAMP01(param);
    obj->is_dirty = true;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    gl_texture_object_t *obj = gl_get_texture_object(target);
    if (obj == NULL) {
        return;
    }

    switch (pname) {
    case GL_TEXTURE_WRAP_S:
        gl_texture_set_wrap_s(obj, param);
        break;
    case GL_TEXTURE_WRAP_T:
        gl_texture_set_wrap_t(obj, param);
        break;
    case GL_TEXTURE_MIN_FILTER:
        gl_texture_set_min_filter(obj, param);
        break;
    case GL_TEXTURE_MAG_FILTER:
        gl_texture_set_mag_filter(obj, param);
        break;
    case GL_TEXTURE_PRIORITY:
        gl_texture_set_priority(obj, I32_TO_FLOAT(param));
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    gl_texture_object_t *obj = gl_get_texture_object(target);
    if (obj == NULL) {
        return;
    }

    switch (pname) {
    case GL_TEXTURE_WRAP_S:
        gl_texture_set_wrap_s(obj, param);
        break;
    case GL_TEXTURE_WRAP_T:
        gl_texture_set_wrap_t(obj, param);
        break;
    case GL_TEXTURE_MIN_FILTER:
        gl_texture_set_min_filter(obj, param);
        break;
    case GL_TEXTURE_MAG_FILTER:
        gl_texture_set_mag_filter(obj, param);
        break;
    case GL_TEXTURE_PRIORITY:
        gl_texture_set_priority(obj, param);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    gl_texture_object_t *obj = gl_get_texture_object(target);
    if (obj == NULL) {
        return;
    }

    switch (pname) {
    case GL_TEXTURE_WRAP_S:
        gl_texture_set_wrap_s(obj, params[0]);
        break;
    case GL_TEXTURE_WRAP_T:
        gl_texture_set_wrap_t(obj, params[0]);
        break;
    case GL_TEXTURE_MIN_FILTER:
        gl_texture_set_min_filter(obj, params[0]);
        break;
    case GL_TEXTURE_MAG_FILTER:
        gl_texture_set_mag_filter(obj, params[0]);
        break;
    case GL_TEXTURE_BORDER_COLOR:
        gl_texture_set_border_color(obj, I32_TO_FLOAT(params[0]), I32_TO_FLOAT(params[1]), I32_TO_FLOAT(params[2]), I32_TO_FLOAT(params[3]));
        break;
    case GL_TEXTURE_PRIORITY:
        gl_texture_set_priority(obj, I32_TO_FLOAT(params[0]));
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    gl_texture_object_t *obj = gl_get_texture_object(target);
    if (obj == NULL) {
        return;
    }

    switch (pname) {
    case GL_TEXTURE_WRAP_S:
        gl_texture_set_wrap_s(obj, params[0]);
        break;
    case GL_TEXTURE_WRAP_T:
        gl_texture_set_wrap_t(obj, params[0]);
        break;
    case GL_TEXTURE_MIN_FILTER:
        gl_texture_set_min_filter(obj, params[0]);
        break;
    case GL_TEXTURE_MAG_FILTER:
        gl_texture_set_mag_filter(obj, params[0]);
        break;
    case GL_TEXTURE_BORDER_COLOR:
        gl_texture_set_border_color(obj, params[0], params[1], params[2], params[3]);
        break;
    case GL_TEXTURE_PRIORITY:
        gl_texture_set_priority(obj, params[0]);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glScissor(GLint left, GLint bottom, GLsizei width, GLsizei height)
{
    if (left < 0 || bottom < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }

    state.scissor_box[0] = left;
    state.scissor_box[1] = bottom;
    state.scissor_box[2] = width;
    state.scissor_box[3] = height;

    state.is_scissor_dirty = true;
}

void glBlendFunc(GLenum src, GLenum dst)
{
    switch (src) {
    case GL_ZERO: 
    case GL_ONE: 
    case GL_DST_COLOR: 
    case GL_ONE_MINUS_DST_COLOR: 
    case GL_SRC_ALPHA: 
    case GL_ONE_MINUS_SRC_ALPHA: 
    case GL_DST_ALPHA: 
    case GL_ONE_MINUS_DST_ALPHA:
    case GL_SRC_ALPHA_SATURATE:
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (dst) {
    case GL_ZERO: 
    case GL_ONE: 
    case GL_DST_COLOR: 
    case GL_ONE_MINUS_DST_COLOR: 
    case GL_SRC_ALPHA: 
    case GL_ONE_MINUS_SRC_ALPHA: 
    case GL_DST_ALPHA: 
    case GL_ONE_MINUS_DST_ALPHA:
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    state.blend_src = src;
    state.blend_dst = dst;
}

void glDrawBuffer(GLenum buf)
{
    switch (buf) {
    case GL_NONE:
    case GL_FRONT_LEFT:
    case GL_FRONT:
    case GL_LEFT:
    case GL_FRONT_AND_BACK:
        state.draw_buffer = buf;
        break;
    case GL_FRONT_RIGHT:
    case GL_BACK_LEFT:
    case GL_BACK_RIGHT:
    case GL_BACK:
    case GL_RIGHT:
    case GL_AUX0:
    case GL_AUX1:
    case GL_AUX2:
    case GL_AUX3:
        gl_set_error(GL_INVALID_OPERATION);
        return;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glClear(GLbitfield buf)
{
    assert_framebuffer();

    rdpq_set_other_modes_raw(SOM_CYCLE_FILL);
    gl_apply_scissor();

    gl_framebuffer_t *fb = state.cur_framebuffer;

    if (buf & GL_DEPTH_BUFFER_BIT) {
        rdpq_set_color_image_no_scissor(fb->depth_buffer, FMT_RGBA16, fb->color_buffer->width, fb->color_buffer->height, fb->color_buffer->width * 2);
        rdpq_set_fill_color(color_from_packed16(state.clear_depth * 0xFFFC));
        rdpq_fill_rectangle(0, 0, fb->color_buffer->width, fb->color_buffer->height);

        rdpq_set_color_image_surface_no_scissor(fb->color_buffer);
    }

    if (buf & GL_COLOR_BUFFER_BIT) {
        rdpq_set_fill_color(RGBA32(
            CLAMPF_TO_U8(state.clear_color[0]), 
            CLAMPF_TO_U8(state.clear_color[1]), 
            CLAMPF_TO_U8(state.clear_color[2]), 
            CLAMPF_TO_U8(state.clear_color[3])));
        rdpq_fill_rectangle(0, 0, fb->color_buffer->width, fb->color_buffer->height);
    }
}

void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    state.clear_color[0] = r;
    state.clear_color[1] = g;
    state.clear_color[2] = b;
    state.clear_color[3] = a;
}

void glClearDepth(GLclampd d)
{
    state.clear_depth = d;
}

void glDepthFunc(GLenum func)
{
    switch (func) {
    case GL_NEVER:
    case GL_LESS:
    case GL_ALWAYS:
        state.depth_func = func;
        break;
    case GL_EQUAL:
    case GL_LEQUAL:
    case GL_GREATER:
    case GL_NOTEQUAL:
    case GL_GEQUAL:
        assertf(0, "Depth func not supported: %lx", func);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glFlush(void)
{
    rspq_flush();
}

void glFinish(void)
{
    rspq_wait();
}

void glGetBooleanv(GLenum value, GLboolean *data)
{
    switch (value) {
    case GL_COLOR_CLEAR_VALUE:
        data[0] = CLAMPF_TO_BOOL(state.clear_color[0]);
        data[1] = CLAMPF_TO_BOOL(state.clear_color[1]);
        data[2] = CLAMPF_TO_BOOL(state.clear_color[2]);
        data[3] = CLAMPF_TO_BOOL(state.clear_color[3]);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        break;
    }
}

void glGetIntegerv(GLenum value, GLint *data)
{
    switch (value) {
    case GL_COLOR_CLEAR_VALUE:
        data[0] = CLAMPF_TO_I32(state.clear_color[0]);
        data[1] = CLAMPF_TO_I32(state.clear_color[1]);
        data[2] = CLAMPF_TO_I32(state.clear_color[2]);
        data[3] = CLAMPF_TO_I32(state.clear_color[3]);
        break;
    case GL_CURRENT_COLOR:
        data[0] = CLAMPF_TO_I32(state.current_color[0]);
        data[1] = CLAMPF_TO_I32(state.current_color[1]);
        data[2] = CLAMPF_TO_I32(state.current_color[2]);
        data[3] = CLAMPF_TO_I32(state.current_color[3]);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        break;
    }
}

void glGetFloatv(GLenum value, GLfloat *data)
{
    switch (value) {
    case GL_COLOR_CLEAR_VALUE:
        data[0] = state.clear_color[0];
        data[1] = state.clear_color[1];
        data[2] = state.clear_color[2];
        data[3] = state.clear_color[3];
        break;
    case GL_CURRENT_COLOR:
        data[0] = state.current_color[0];
        data[1] = state.current_color[1];
        data[2] = state.current_color[2];
        data[3] = state.current_color[3];
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        break;
    }
}

void glGetDoublev(GLenum value, GLdouble *data)
{
    switch (value) {
    case GL_COLOR_CLEAR_VALUE:
        data[0] = state.clear_color[0];
        data[1] = state.clear_color[1];
        data[2] = state.clear_color[2];
        data[3] = state.clear_color[3];
        break;
    case GL_CURRENT_COLOR:
        data[0] = state.current_color[0];
        data[1] = state.current_color[1];
        data[2] = state.current_color[2];
        data[3] = state.current_color[3];
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        break;
    }
}

GLubyte *glGetString(GLenum name)
{
    switch (name) {
    case GL_VENDOR:
        return (GLubyte*)"Libdragon";
    case GL_RENDERER:
        return (GLubyte*)"N64";
    case GL_VERSION:
        return (GLubyte*)"1.1";
    case GL_EXTENSIONS:
        return (GLubyte*)"GL_EXT_packed_pixels";
    default:
        gl_set_error(GL_INVALID_ENUM);
        return NULL;
    }
}
