#ifndef __LIBDRAGON_YUV_H
#define __LIBDRAGON_YUV_H

/**
 * @brief Convert YUV frames to RGB.
 * 
 * This library contains hardware-accelerated function to convert a YUV image
 * into a RGB image. The task is mainly performed using RDP, but the RSP can
 * also be used to handle parts of it.
 * 
 * To be able to use it efficiently with different video codecs, the library
 * supports the input planes in different format. Specifically:
 * 
 *  * #yuv_draw_frame_3p converts a frame made with Y, U, V in separate buffers,
 *    with chroma subsampling 4:2:0 (U,V are half the size of Y, both horizontally
 *    and vertically).
 *  * #yuv_draw_frame_2p converts a frame made with Y in a first buffer, and UV
 *    interleaved in a second buffer. Again, this assumes chroma subsampling
 *    4:2:0.
 *  * #yuv_drame_frame_1p converts a frame made with all planes pre-interleaved
 *    in a single buffer, using the pattern YUYV. This uses chroma subsampling
 *    4:2:2 (UV has half the resolution of Y horizontally, but the same
 *    resolution vertically).
 * 
 * The most efficient is #yuv_draw_frame_2p, which requires no additional
 * memory, and has the smallest impact on memory bandwidth. If possible,
 * arrange for the input data to be organized in this format. For instance,
 * a video codec might decompress the chroma channels via RSP in DMEM, and
 * pre-interleave them before sending them back to RDRAM. If the codec
 * creates the U and V planes separately, then calling #yuv_draw_frame_3p
 * is a good alternative: it will interleave the channels using RSP with
 * very efficient code, but requires an intermediate buffer to do so.
 * 
 */

#include <stdint.h>
#include "graphics.h"
#include "rspq.h"


void yuv_init(void);
void yuv_close(void);


/**
 * @brief A YUV colorspace
 * 
 * This structure contains the parameters that define a YUV colorspace
 * for conversion to and from the RGB space. The "c" parameters are
 * used when doing a CPU-based conversion (using #yuv_to_rgb), while
 * the "k" parameters are used when doing a RDP-based conversion
 * (see all the yuv_draw_frame_* functions).
 * 
 * Most users can simply use one of the predefined colorspaces: #YUV_BT601_TV,
 * #YUV_BT601_FULL, #YUV_BT709_TV, #YUV_BT709_FULL. To simplify creating a custom
 * colorspace, #yuv_new_colorspace can be used.
 * 
 * When playing back a video, you should specify the colorspace that was used
 * to encode the video. Normally, this is available in the video header or 
 * stream as metadata information. Notice that most video encoders such as
 * ffmpeg default to ITU-R BT601 TV Range when encoding low resolution movies
 * (non-HD resolutions), so using #YUV_BT601_TV is a good default if the exact
 * colorspace is not known.
 * 
 * When encoding a video, it is suggested to configure the encoder to use
 * ITU-R BT601 TV Range. In general, colorspaces created in the "TV Range"
 * have more color fidelity when displayed on a CRT TV. For PC-only playback
 * through emulators, "Full Range" colorspaces offer more color precision.
 */
typedef struct {
	float c0, c1, c2, c3, c4; int y0;
	int k0, k1, k2, k3, k4, k5;
} yuv_colorspace_t;

/** @brief ITU-R BT.601 TV Range colorspace (DEFAULT). 
 * 
 * This is the standard colorspace used by low resolution videos such as those
 * normally encoded for Nintendo 64. If in doubt, try first always with this
 * colorspace.
 * 
 * Created via #yuv_new_colorspace using the following parameters:
 *    
 *  * Kr=0.299, Kb=0.114
 *  * Y0=16, yrange=219, crange=224
 * 
 */ 
extern const yuv_colorspace_t YUV_BT601_TV;

/** @brief ITU-R BT.601 Full Range colorspace. 
 *
 * Created via #yuv_new_colorspace using the following parameters:
 *    
 *  * Kr=0.299, Kb=0.114
 *  * Y0=0, yrange=256, crange=256
 */
extern const yuv_colorspace_t YUV_BT601_FULL;

/** @brief ITU-R BT.709 TV Range colorspace. 
 * 
 * Created via #yuv_new_colorspace using the following parameters:
 *    
 *  * Kr=0.0722, Kb=0.2126
 *  * Y0=16, yrange=219, crange=224
 */
extern const yuv_colorspace_t YUV_BT709_TV;

/** 
 * @brief ITU-R BT.709 Full Range colorspace.
 * 
 * Created via #yuv_new_colorspace using the following parameters:
 *    
 *  * Kr=0.0722, Kb=0.2126
 *  * Y0=0, yrange=256, crange=256
 */
extern const yuv_colorspace_t YUV_BT709_FULL;


/**
 * @brief Calculate coefficients for a new YUV colorspace.
 * 
 * This function is mostly for documentation purposes. It can be used to create
 * a new colorspace, by calculating its coefficients (stored into #yuv_colorspace_t)
 * from the mathematical definition of the colorspace. Most users will not need
 * to call this function and just use one of the predefined colorspaces such as
 * #YUV_BT601_TV.
 * 
 * A YUV colorspace is defined by three constants normally called Kr, Kg, Kb.
 * Since the sum of these three constants must be 1.0, normally only two are
 * provided (by convention, Kr and Kb).
 * 
 * Because of technical issues with old analog TVs, it was not possible to 
 * display the full 8-bit range of the Y,U,V components, so during the conversion
 * the range was often restricted a bit. The range for the Y component is defined
 * via the minimum allowed value (y0) and the number of possible values
 * (yrange -- so the last allowed value is y0+yrange-1). The range for the
 * U,V components is specified in "crange" (no minimum value can be specified
 * because the U,V components are actually interpreted as signed numbers, 
 * centered on 0).
 * 
 * For old TVs, colorspaces should use the "TV Range" which is defined as
 * y0=19, yrange=219, crange=224.
 * 
 * Videos meant to be played back on PC are probably coded using the "Full Range",
 * which is y0=0, yrange=256, crange=256.
 *
 * @param      cs      The colorspace structure that will be filled.
 * @param[in]  Kr      The colorspace coefficient Kr
 * @param[in]  Kb      The colorspace coefficient Kb
 * @param[in]  y0      The minimum allowed value for the Y component
 * @param[in]  yrange  The number of allowed values for the Y component
 * @param[in]  crange  The number of allowed values for the U,V component
 * 
 * @see #YUV_BT601_TV
 * @see #YUV_BT601_FULL
 * @see #YUV_BT709_TV
 * @see #YUV_BT709_FULL
 * 
 */
void yuv_new_colorspace(yuv_colorspace_t *cs, float Kr, float Kb, int y0, int yrange, int crange);


/**
 * @brief Convert a single YUV pixel into RGB.
 * 
 * Convert a single YUV pixel to RGB, using the CPU. This function
 * should be used only for non-performance critical tasks. For high-performance
 * conversions, see the yuv_draw_frame_* functions that are hardware
 * accelerated via the RDP.
 * 
 * Notice that this function is not meant to be bit-exact with the RDP hardware
 * accelerated version, but it will return values which are very close.
 *
 * @param[in]  y     Y component
 * @param[in]  u     U component
 * @param[in]  v     V component
 * @param      cs    The colorspace to use for the conversion
 *
 * @return     The converted pixel in RGBA format (A is forced to 255).
 */
color_t yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, const yuv_colorspace_t *cs);


/**
 * @brief Chroma subsampling used for the input U/V buffers.
 * 
 * Normally, in YUV pictures, chroma planes (U, V) are subsampled, that is
 * they are encoded in a lower resolution compared to luminance (Y).
 * 
 * This library supports two types of subsampling which are the ones supported
 * by RDP: 4:2:0 (chroma planes have half the resolution both horizontally
 * and vertically, which is the default for JPEG, MJPEG, MPEG1/2, ecc.) and
 * 4:2:2 (chroma planes have half the resolution horizontally, and same
 * resolution vertically).
 */
typedef enum {
	/** Chroma subsampling 4:2:0: chroma resolution is half the luminance
     * both horizontally and vertically. */
	YUV_CHRSUB_420,

	/** Chroma subsampling 4:2:0: chroma resolution is half the luminance
	 * horizontally, and same resolution vertically. */
	YUV_CHRSUB_422,
} yuv_chroma_subsampling_t;


/** @brief YUV blitter zoom configuration */
typedef enum {
	YUV_ZOOM_KEEP_ASPECT,  ///< Zoom the frame, keeping frame aspect ratio
	YUV_ZOOM_FULL,         ///< Zoom the frame, irrespective of aspect ratio
    YUV_ZOOM_NONE,         ///< Do not zoom the frame to fit the output buffer
} yuv_zoom_t;

/** @brief YUV blitter output buffer alignment */
typedef enum {
	YUV_ALIGN_CENTER,   ///< Align to center of the output buffer
	YUV_ALIGN_MIN,      ///< Align to left/top of the output buffer
	YUV_ALIGN_MAX,      ///< Align to right/bottom of the output buffer
} yuv_align_t;


/** @brief YUV blitter configuration. */
typedef struct {
	/** @brief Width of the input buffer (Y plane) */
	int width;

	/** @brief Height of the input buffer (Y plane) */
	int height;

	/** @brief Subsampling of the input U/V chroma buffers. */
	yuv_chroma_subsampling_t sub;

	/** @brief Color space to use during conversion (if NULL, default to #YUV_BT601_TV) */
	const yuv_colorspace_t *cs;

	/** @brief Frame horizontal alignment to the output buffer */
	yuv_align_t halign;

	/** @brief Frame vertical alignment to the output buffer */
	yuv_align_t valign;

	/** @brief Frame zooming algorithm to use. */
	yuv_zoom_t zoom;

	/** @brief Background color to be used to clear the portions of the output
	 *         buffer not covered by the frame.
     *
	 * After drawing the frame, the portions of the buffer not covered with
	 * pixels (if any) will be cleared using this color.
	 */
	color_t bkg_color;

	// FIXME: remove this by reading the framebuffer size from rdp.c
	int out_width, out_height; 

} yuv_config_t;


/**
 * @brief Draw a 3-planes YUV frame into the current RDP framebuffer.
 * 
 * This function performs a YUV->RGB conversion for a full frame. The input
 * is expected as 3 separates 8-bpp planes for the 3 components (YUV), with the
 * U/V planes being exactly half the width and the height of the Y plane
 * (as per standard 4:2:0 chroma subsampling). The output is drawn into the
 * currently-attached RDP display buffer.
 * 
 * Internally, the function uses the RSP to interleave the U and V plane
 * together into an intermediate buffer, and then uses the RDP to perform
 * the actual conversion and blitting. The buffer is allocated on the heap
 * and has size width * height / 2.
 * 
 * Assuming a 32-bit framebuffer, the impact on memory bandwidth (number of
 * RDRAM bytes touched during the execution) is width * height * 6.5.
 * 
 * @param      cfg     YUV blitter configuration
 * @param      y       Pointer to the y plane
 * @param      u       Pointer to the u plane. Width/height must be exactly
 *                     half of that of y frame.
 * @param      v       Pointer to the v plane. Width/height must be exactly
 *                     half of that of y frame.
 *
 */
void yuv_draw_frame_3p(yuv_config_t *cfg, uint8_t *y, uint8_t *u, uint8_t *v);


/**
 * @brief Draw a 2-planes YUV frame into the current RDP framebuffer.
 * 
 * This function performs a YUV->RGB conversion for a full frame. The input
 * is expected as 2 separates 8-bpp planes for the 3 components: one plane
 * with the Y component, and 1 plane that contains interleaved UV components.
 * Since U/V planes (separately) are half the width and the height of the Y plane
 * (as per standard 4:2:0 chroma subsampling), the interleaved UV plane must
 * have the same width of Y plane, and half the height. The output is drawn
 * into the currently-attached RDP display buffer.
 * 
 * Internally, the function uses the RDP to perform the actual
 * conversion and blitting. No usage of RSP is needed, and no additional memory
 * is allocated.
 * 
 * Assuming a 32-bit framebuffer, the impact on memory bandwidth (number of
 * RDRAM bytes touched during the execution) is width * height * 5.5.
 *
 * @param      cfg     YUV blitter configuration
 * @param      y       Pointer to the y plane
 * @param      uv      Pointer to the u plane. Width must be the same of y plane,
 *                     while height must be half of y plane.
 *
 */
void yuv_draw_frame_2p(uint8_t *y, uint8_t *uv);

/**
 * @brief Draw a 1-plane YUYV frame into the current RDP framebuffer.
 * 
 * This function performs a YUV->RGB conversion for a full frame. The input
 * is expected as one interleaved plane for the 3 components: it must contain
 * the components in the order YUYV. This corresponds to a 4:2:2 chroma
 * subsampling: each U/V component has half the horizontal resolution
 * compared to Y, but the same vertical resolution. The output is drawn into the
 * currently-attached RDP display buffer.
 * 
 * Internally, the function uses the RDP to perform the actual
 * conversion and blitting. No usage of RSP is needed, and no additional memory
 * is allocated.
 * 
 * Assuming a 32-bit framebuffer, the impact on memory bandwidth (number of
 * RDRAM bytes touched during the execution) is width * height * 5.5.
 *
 * @param      cfg     YUV blitter configuration
 * @param      yuyv    Pointer to the yuyv plane
 *
 */
void yuv_draw_frame_1p(uint8_t *yuyv);


void yuv_set_input_buffer(uint8_t *y, uint8_t *cb, uint8_t *cr, int y_pitch);
void yuv_set_output_buffer(uint8_t *out, int out_pitch);
void yuv_interleave4_block_32x16(int x0, int y0);
void yuv_interleave2_block_32x16(int x0, int y0);

#endif
