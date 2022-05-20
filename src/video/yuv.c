#include "yuv.h"
#include "yuv_internal.h"
#include "rsp.h"
#include "rdp.h"
#include "rdp_commands.h"
#include "rspq.h"
#include "n64sys.h"
#include "debug.h"
#include "utils.h"
#include <math.h>

#define BLOCK_W 32
#define BLOCK_H 16

static uint8_t *internal_buffer = NULL;
static int internal_buffer_size = 0;

// Calculated with: yuv_new_colorspace(&cs, 0.299, 0.114, 16, 219, 224);
const yuv_colorspace_t YUV_BT601_TV = {
	.c0=1.16895, .c1=1.60229, .c2=-0.393299, .c3=-0.816156, .c4=2.02514, .y0=16,
	.k0=175, .k1=-43, .k2=-89, .k3=222, .k4=111, .k5=43
};

// Calculated with: yuv_new_colorspace(&cs, 0.299, 0.114, 0, 256, 256);
const yuv_colorspace_t YUV_BT601_FULL = {
	.c0=1, .c1=1.402, .c2=-0.344136, .c3=-0.714136, .c4=1.772, .y0=0,
	.k0=179, .k1=-44, .k2=-91, .k3=227, .k4=0, .k5=0
};

// Calculated with: yuv_new_colorspace(&cs, 0.2126, 0.0722, 16, 219, 224);
const yuv_colorspace_t YUV_BT709_TV = {
	.c0=1.16895, .c1=1.79977, .c2=-0.214085, .c3=-0.534999, .c4=2.12069, .y0=16,
	.k0=197, .k1=-23, .k2=-59, .k3=232, .k4=111, .k5=43
};

// Calculated with: yuv_new_colorspace(&cs, 0.2126, 0.0722, 16, 219, 224);
const yuv_colorspace_t YUV_BT709_FULL = {
	.c0=1, .c1=1.5748, .c2=-0.187324, .c3=-0.468124, .c4=1.8556, .y0=0,
	.k0=202, .k1=-24, .k2=-60, .k3=238, .k4=0, .k5=0
};


static void resize_internal_buffer(int size)
{
	if (internal_buffer_size < size) {
		internal_buffer_size = size;
		if (internal_buffer) free_uncached(internal_buffer);
		internal_buffer = malloc_uncached(size);
	}
}

static void yuv_assert_handler(rsp_snapshot_t *state, uint16_t code) {
	switch (code) {
	case ASSERT_INVALID_INPUT_Y:
		printf("Input buffer for Y plane was not configured\n");
		break;
	case ASSERT_INVALID_INPUT_CB:
		printf("Input buffer for CB plane was not configured\n");
		break;
	case ASSERT_INVALID_INPUT_CR:
		printf("Input buffer for CR plane was not configured\n");
		break;
	case ASSERT_INVALID_OUTPUT:
		printf("Output buffer was not configured\n");
		break;
	}
}

DEFINE_RSP_UCODE(rsp_yuv,
	.assert_handler = yuv_assert_handler);

#define CMD_YUV_SET_INPUT          0x40
#define CMD_YUV_SET_OUTPUT         0x41
#define CMD_YUV_INTERLEAVE4_32X16  0x42
#define CMD_YUV_INTERLEAVE2_32X16  0x43

static bool yuv_initialized = false;

void yuv_init(void)
{
	if (yuv_initialized)
		return;

	rspq_init();
	rspq_overlay_register(&rsp_yuv, 0x4);
	yuv_initialized = true;
}

void yuv_close(void)
{
	if (internal_buffer) {
		free_uncached(internal_buffer);
		internal_buffer = NULL;
		internal_buffer_size = 0;
	}

	yuv_initialized = false;
}

void yuv_new_colorspace(yuv_colorspace_t *cs, float kr, float kb, int y0i, int yrangei, int crangei)
{
    // Matrix from: https://en.wikipedia.org/wiki/YCbCr#YCbCr
	float kg = 1.0f - kr - kb;
	float m[3][3] = {
        {      kr,                      kg,                   kb,          }, 
        { -0.5f*kr/(1.0f-kb),    -0.5f*kg/(1.0f-kb),         0.5f,         },
        {    0.5f,               -0.5f*kg/(1.0f-kr),    -0.5f*kb/(1.0f-kr) },
	};

    // Invert matrix
    float idet = 1.0f / 
         (m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
          m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
          m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]));
	float im[3][3] = {
        {(m[1][1] * m[2][2] - m[2][1] * m[1][2]) * idet,
        (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * idet,
        (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * idet},
        {(m[1][2] * m[2][0] - m[1][0] * m[2][2]) * idet,
        (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * idet,
        (m[1][0] * m[0][2] - m[0][0] * m[1][2]) * idet},
        {(m[1][0] * m[2][1] - m[2][0] * m[1][1]) * idet,
        (m[2][0] * m[0][1] - m[0][0] * m[2][1]) * idet,
        (m[0][0] * m[1][1] - m[1][0] * m[0][1]) * idet}
	};

	// Bring range arguments into 0..1 range
	float y0 = y0i * (1.0f / 255.0f);
	float yrange = 256.0f / yrangei;
	float crange = 256.0f / crangei;

	// Using im, we can convert YUV to RGB using a standard
	// matrix multiplication:
	// 
	//   RGB = YUV * im
	//   
	// Fortunately, most elements of the matrix are 0, so we
	// can save a few multiplications and end up with this
	// formula:
	// 
    // Which simplify our formula:
    // 
    //   R = C0 * Y + C1*V
    //   G = C0 * Y + C2*U + C3*V
    //   B = C0 * Y + C4*U
	//   
	// This does not take the range into account. To do so,
	// we can adjust Y by y0, and then pre-multiply yrange
	// into C0, and crange into C1..C4. The final
	// formula will be:
	// 
    //   R = C0 * (Y-y0) + C1*V
    //   G = C0 * (Y-y0) + C2*U + C3*V
    //   B = C0 * (Y-y0) + C4*U
	//   
	// which is the one used by #yuv_to_rgb.
	// 
	cs->c0 = im[0][0] * yrange;
	cs->c1 = im[0][2] * crange;
	cs->c2 = im[1][1] * crange;
	cs->c3 = im[1][2] * crange;
	cs->c4 = im[2][1] * crange;
	cs->y0 = y0i;

	// Now calculate the RDP coefficients. 
    // The RDP cannot do exactly this formula. What the RDP does is
    // slightly different, and it does it in two steps. The first step is
    // the texture filter, which calculates:
    // 
    //    TF_R = Y + K0*V
    //    TF_G = Y + K1*U + K2*V
    //    TF_B = Y + K3*U
    // 
    // The second step is the color combiner, which will use the following
    // formula:
    // 
    //    R = (TF_R - K4) * K5 + TF_R = (TF_R - (K4*K5)/(1+K5)) * (1+K5)
    //    G = (TF_G - K4) * K5 + TF_G = (TF_G - (K4*K5)/(1+K5)) * (1+K5)
    //    B = (TF_B - K4) * K5 + TF_B = (TF_B - (K4*K5)/(1+K5)) * (1+K5)
    //    
    // By concatenating the two steps, we find:
    // 
    //    R = (Y + K0*V        - (K4*K5)/(1+K5))) * (1+K5)
    //    G = (Y + K1*U + K2*V - (K4*K5)/(1+K5))) * (1+K5)
    //    B = (Y + K3*U        - (K4*K5)/(1+K5))) * (1+K5)
    // 
    // So let's now compare this with the standard formula above. We need to find
    // a way to express K0..K5 in terms of C0..C4 (plus y0). Let's take
    // the standard formula and factor C0:
    // 
    //    R = (Y - y0 + C1*V/C0)           * C0
    //    G = (Y - y0 + C2*U/C0 + C3*V/C0) * C0
    //    B = (Y - y0 + C4*U/C0)           * C0
    //    
    // We can now derive all coefficients:
    //   
    //    1+K5 = C0              =>    K5 = C0 - 1
    //    (K4*K5)/(1+K5) = y0    =>    K4 = (y0 * (1+K5)) / K5) = y0/K5 + y0
    //
    //    K0 = C1 / C0
    //    K1 = C2 / C0
    //    K2 = C3 / C0
    //    K3 = C4 / C0
    // 
	float ic0 = 1.0f / cs->c0;
	float k5 = cs->c0 - 1;
	float k4 = k5 != 0 ? y0 / k5 + y0 : 0;
	float k0 = cs->c1 * ic0;
	float k1 = cs->c2 * ic0;
	float k2 = cs->c3 * ic0;
	float k3 = cs->c4 * ic0;
	cs->k0 = roundf(k0*128.f);
	cs->k1 = roundf(k1*128.f);
	cs->k2 = roundf(k2*128.f);
	cs->k3 = roundf(k3*128.f);
	cs->k4 = roundf(k4*255.f);
	cs->k5 = roundf(k5*255.f);
}

color_t yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, const yuv_colorspace_t *cs)
{
	float yp = (y - cs->y0) * cs->c0;
	float r = yp + cs->c1 * (v-128) + .5f;
	float g = yp + cs->c2 * (u-128) + cs->c3 * (v-128) + .5f;
	float b = yp + cs->c4 * (u-128) + .5f;

	debugf("%d,%d,%d => %f,%f,%f\n", y, u, v, r, g, b);

	return (color_t){
		.r = r > 255 ? 255.f : r < 0 ? 0 : r,
		.g = g > 255 ? 255.f : g < 0 ? 0 : g,
		.b = b > 255 ? 255.f : b < 0 ? 0 : b,
		.a = 0xFF,
	};
}

void rsp_yuv_set_input_buffer(uint8_t *y, uint8_t *cb, uint8_t *cr, int y_pitch)
{
	rspq_write(CMD_YUV_SET_INPUT, 
		PhysicalAddr(y), PhysicalAddr(cb), PhysicalAddr(cr), y_pitch);
}

void rsp_yuv_set_output_buffer(uint8_t *out, int out_pitch)
{
	rspq_write(CMD_YUV_SET_OUTPUT,
		PhysicalAddr(out), out_pitch);
}

void rsp_yuv_interleave4_block_32x16(int x0, int y0)
{
	rspq_write(CMD_YUV_INTERLEAVE4_32X16,
		(x0<<12) | y0);
}

void rsp_yuv_interleave2_block_32x16(int x0, int y0)
{
	rspq_write(CMD_YUV_INTERLEAVE2_32X16,
		(x0<<12) | y0);
}

static void cfg_chroma_size(yuv_config_t *cfg, int *uv_width, int *uv_height)
{
	switch (cfg->sub) {
	case YUV_CHRSUB_420:
		*uv_width = cfg->width / 2;
		*uv_height = cfg->height / 2;
		break;
	case YUV_CHRSUB_422:
		*uv_width = cfg->width / 2;
		*uv_height = cfg->height;
		break;
	default:
		assertf(0, "invalid yuv config: sub=%d", cfg->sub);
	}
}

void yuv_draw_frame_3p(yuv_config_t *cfg, uint8_t *ybuf, uint8_t *ubuf, uint8_t *vbuf)
{
	int width = cfg->width, height = cfg->height;

	int uv_width, uv_height;
	cfg_chroma_size(cfg, &uv_width, &uv_height);

	// Make sure we have the internal buffer ready. We will interleave U and V
	// planes so we need a buffer that handles two of those planes at the same time.
	resize_internal_buffer(uv_width*uv_height*2);

	// Get output size. FIXME: use rdp.h functions to get the size of the
	// attached framebuffer, rather than the display.
	int screen_width = cfg->out_width ? cfg->out_width : display_get_width();
	int screen_height = cfg->out_height ? cfg->out_height : display_get_height();

	// Calculate the size that the picture will have after blitting (after zoom).
	int video_width = cfg->width;
	int video_height = cfg->height;
	float scalew = 1.0f, scaleh = 1.0f;

	if (cfg->zoom != YUV_ZOOM_NONE && width < screen_width && height < screen_height) {
		scalew = (float)screen_width / (float)width;
		scaleh = (float)screen_height / (float)height;
		if (cfg->zoom == YUV_ZOOM_KEEP_ASPECT)
			scalew = scaleh = MIN(scalew, scaleh);

		video_width = width * scalew;
		video_height = height *scaleh;
	}

	// Calculate first pixel covered in the screen
	int xstart, ystart;
	switch (cfg->halign) {
	case YUV_ALIGN_CENTER: xstart = (screen_width - video_width) / 2;   break;
	case YUV_ALIGN_MIN:    xstart = 0;                                  break;
	case YUV_ALIGN_MAX:    xstart = screen_width - video_width;         break;
	default: assertf(0, "invalid yuv config: halign=%d", cfg->halign);
	}
	switch (cfg->valign) {
	case YUV_ALIGN_CENTER: ystart = (screen_height - video_height) / 2; break;
	case YUV_ALIGN_MIN:    ystart = 0;                                  break;
	case YUV_ALIGN_MAX:    ystart = screen_height - video_height;       break;
	default: assertf(0, "invalid yuv config: valign=%d", cfg->valign);
	}

	// Clear the screen. To save fillrate, we just clear the part outside
	// of the image that we will draw (if any).
	rdp_set_clipping(0, 0, screen_width, screen_height);
	if (screen_height > video_height || screen_width > video_width) {
		rdp_sync_pipe();
		rdp_set_other_modes(SOM_CYCLE_FILL);
		rdp_set_fill_color(graphics_convert_color(cfg->bkg_color));
		if (ystart > 0)
			rdp_fill_rectangle(0, 0, (screen_width-1)<<2, (ystart-1)<<2);
		if (ystart+video_height < screen_height)
			rdp_fill_rectangle(0, (ystart+video_height)<<2, (screen_width-1)<<2, (screen_height-1)<<2);
		if (xstart > 0)
			rdp_fill_rectangle(0, ystart<<2, (xstart+1)<<2, (ystart+video_height-1)<<2);
		if (xstart+video_width < screen_width)
			rdp_fill_rectangle((xstart+video_width)<<2, ystart<<2, (screen_width-1)<<2, (ystart+video_height-1)<<2);
	}

	#if 0
	for (int y=0; y < height/2; y++) {
		for (int x=0; x < width/2; x ++) {
			internal_buffer[(y+0)*width + x*2 + 0] = frame->cb.data[y*width/2 + x];
			internal_buffer[(y+0)*width + x*2 + 1] = frame->cr.data[y*width/2 + x];
		}
	}
	#else

	// Interleave U and V planes into the internal buffer.
	rsp_yuv_set_input_buffer(ybuf, ubuf, vbuf, width);
	rsp_yuv_set_output_buffer(internal_buffer, uv_width*2);
	for (int y=0; y < height; y += 16) {
		for (int x=0; x < width; x += 32) {
			// FIXME: for now this only works with subsampling 4:2:0
			assert(cfg->sub == YUV_CHRSUB_420);
			rsp_yuv_interleave2_block_32x16(x, y);
		}
		rspq_flush();
	}

	#endif

	// Configure YUV blitting mode
	rdp_sync_pipe();
	rdp_set_other_modes(SOM_CYCLE_1 | SOM_RGBDITHER_NONE | SOM_TC_CONV);
	rdp_set_combine_mode(Comb1_Rgb(TEX0, K4, K5, TEX0));

	const yuv_colorspace_t *cs = cfg->cs ? cfg->cs : &YUV_BT601_TV;
	rdp_set_convert(cs->k0, cs->k1, cs->k2, cs->k3, cs->k4, cs->k5);

	// Tile 0/1: Draw YUV picture (two lines)
	rdp_set_tile(RDP_TILE_FORMAT_YUV, RDP_TILE_SIZE_16BIT, BLOCK_W/8,    0,               0, 0, 0,0,0,0, 0,0,0,0);
	rdp_set_tile(RDP_TILE_FORMAT_YUV, RDP_TILE_SIZE_16BIT, BLOCK_W/8,    width/8,         1, 0, 0,0,0,0, 0,0,0,0);

	// Tile 4/5: Load interleaved U/V buffers (two lines -- duplicated data in case of subsampling 4:2:0)
	rdp_set_tile(RDP_TILE_FORMAT_I,   RDP_TILE_SIZE_8BIT, BLOCK_W/8,     0,               4, 0, 0,0,0,0, 0,0,0,0);
	rdp_set_tile(RDP_TILE_FORMAT_I,   RDP_TILE_SIZE_8BIT, BLOCK_W/8,     width/8,         5, 0, 0,0,0,0, 0,0,0,0);

	// Tile 6/7: Load Y buffer (two lines)
	rdp_set_tile(RDP_TILE_FORMAT_I,   RDP_TILE_SIZE_8BIT, BLOCK_W/8,     2048/8,          6, 0, 0,0,0,0, 0,0,0,0);
	rdp_set_tile(RDP_TILE_FORMAT_I,   RDP_TILE_SIZE_8BIT, BLOCK_W/8,     (2048+width)/8,  7, 0, 0,0,0,0, 0,0,0,0);

	int stepx = (int)(1024.0f / scalew);
	int stepy = (int)(1024.0f / scaleh);

	// Block to copy: 2 lines at a time (width x 2)
	int bw = width; int bh = 2;
	rdp_set_tile_size(0, 0<<2, 0<<2, (width-1)<<2, (0+bh-1)<<2);
	rdp_set_tile_size(1, 0<<2, 0<<2, (width-1)<<2, (0+bh-1)<<2);

	for (int y=0; y<height; y+=bh) {
		int x = 0;
		int sx0 = x * scalew;
		int sx1 = (x+bw) * scalew;

		int sy0 = (y+0) * scaleh;
		int sy1 = (y+1) * scaleh;
		int sy2 = (y+2) * scaleh;

		rdp_sync_tile();
		rdp_sync_load();

		rdp_set_texture_image(PhysicalAddr(ybuf), RDP_TILE_FORMAT_I, RDP_TILE_SIZE_8BIT, width-1);
		rdp_load_block(6, x, y+0, x+bw*bh-1, 0);

		rdp_set_texture_image(PhysicalAddr(internal_buffer), RDP_TILE_FORMAT_I, RDP_TILE_SIZE_8BIT, width-1);
		rdp_load_block(4, x, y/2, x+bw-1, 0);
		rdp_load_block(5, x, y/2, x+bw-1, 0);

		rdp_texture_rectangle(0,
			(sx0+xstart)<<2, (sy0+ystart)<<2,
			(sx1+xstart)<<2, (sy1+ystart)<<2,
			0<<5, 0<<5, stepx, stepy);
		rdp_texture_rectangle(1,
			(sx0+xstart)<<2, (sy1+ystart)<<2,
			(sx1+xstart)<<2, (sy2+ystart)<<2,
			0<<5, 0<<5, stepx, stepy);
	}
}
