#ifndef __MAGMA_FIXED_FUNCTION_CONSTANTS
#define __MAGMA_FIXED_FUNCTION_CONSTANTS

#define MGFX_BINDING_FOG                0
#define MGFX_BINDING_LIGHTING           1
#define MGFX_BINDING_TEXTURING          2
#define MGFX_BINDING_MODES              3
#define MGFX_BINDING_MATRICES           4
#define MGFX_BINDING_MATRIX_PALETTE     5

#define MGFX_LIGHT_COUNT_MAX    8
#define MGFX_MATRIX_SIZE        64

#define MGFX_VTX_SIZE_SHIFT     4

#define MGFX_VTX_POS            0
#define MGFX_VTX_NORM           6
#define MGFX_VTX_RGBA           8
#define MGFX_VTX_TEX            12
#define MGFX_VTX_SIZE           (1<<MGFX_VTX_SIZE_SHIFT)

#define MGFX_VTX_POS_SHIFT      5

#endif