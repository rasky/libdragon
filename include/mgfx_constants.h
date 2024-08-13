#ifndef __MGFX_CONSTANTS
#define __MGFX_CONSTANTS

#define MGFX_ATTRIBUTE_POS_NORM         0
#define MGFX_ATTRIBUTE_COLOR            1
#define MGFX_ATTRIBUTE_TEXCOORD         2

#define MGFX_BINDING_FOG                0
#define MGFX_BINDING_LIGHTING           1
#define MGFX_BINDING_TEXTURING          2
#define MGFX_BINDING_MODES              3
#define MGFX_BINDING_MATRICES           4

#define MGFX_FLAG_FOG               (1<<0)
#define MGFX_FLAG_ENV_MAP           (1<<1)

#define MGFX_LIGHT_COUNT_MAX    8

#define MGFX_LIGHT_POSITION     0
#define MGFX_LIGHT_COLOR        8
#define MGFX_LIGHT_ATT_INT      16
#define MGFX_LIGHT_ATT_FRAC     24
#define MGFX_LIGHT_SIZE         32

#define MGFX_MATRIX_SIZE        64

#define MGFX_VTX_POS_SHIFT      5

#endif
