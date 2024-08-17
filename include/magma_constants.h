#ifndef __MAGMA_CONSTANTS
#define __MAGMA_CONSTANTS

#define MG_INLINE_UNIFORM_HEADER        12
#define MG_MAX_UNIFORM_PAYLOAD_SIZE     (RSPQ_MAX_COMMAND_SIZE*4 - MG_INLINE_UNIFORM_HEADER)

#define MG_VERTEX_CACHE_COUNT           32

#define MG_DEFAULT_GUARD_BAND           2

#define MG_CLIP_PLANE_COUNT             6
#define MG_CLIP_PLANE_SIZE              8
#define MG_CLIP_CACHE_SIZE              9

#define MG_VTX_XYZ                      0
#define MG_VTX_CLIP_CODE                6
#define MG_VTX_TR_CODE                  7
#define MG_VTX_RGBA                     8
#define MG_VTX_ST                       12
#define MG_VTX_Wi                       16
#define MG_VTX_Wf                       18
#define MG_VTX_INVWi                    20
#define MG_VTX_INVWf                    22
#define MG_VTX_CS_POSi                  24
#define MG_VTX_CS_POSf                  32
#define MG_VTX_SIZE                     40

#endif
