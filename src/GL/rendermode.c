#include "gl_internal.h"
#include "rdpq.h"

extern gl_state_t state;

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

void gl_update_render_mode()
{
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

    if (state.blend) {
        // TODO: derive the blender config from blend_src and blend_dst
        modes |= SOM_BLENDING | Blend(PIXEL_RGB, MUX_ALPHA, MEMORY_RGB, INV_MUX_ALPHA);
    }
    
    if (state.texture_2d) {
        modes |= SOM_TEXTURE_PERSP | SOM_TC_FILTER;

        tex_format_t fmt = gl_texture_get_format(&state.texture_2d_object);

        gl_texture_object_t *tex_obj = &state.texture_2d_object;

        if (tex_obj->mag_filter == GL_LINEAR) {
            modes |= SOM_SAMPLE_2X2;
        }

        rdpq_set_combine_mode(Comb_Rgb(TEX0, ZERO, SHADE, ZERO) | Comb_Alpha(TEX0, ZERO, SHADE, ZERO));

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
        rdpq_set_combine_mode(Comb_Rgb(ONE, ZERO, SHADE, ZERO) | Comb_Alpha(ONE, ZERO, SHADE, ZERO));
    }

    rdpq_set_other_modes(modes);
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