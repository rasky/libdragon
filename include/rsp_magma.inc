#include <rsp_queue.inc>
#include "magma_constants.h"

#define TRIANGLE_SIZE       0xB0
#define MAX_UNIFORMS_SIZE   0x400

.macro MgBeginShaderUniforms
    .data

    RSPQ_BeginSavedState
    
    .align 3
    MAGMA_VERTEX_CACHE:         .dcb.b  MAGMA_VERTEX_CACHE_COUNT * MAGMA_VTX_SIZE
    MAGMA_VERTEX_OVERFLOW:      .dcb.b  MAGMA_VTX_SIZE

    .align 4
MAGMA_STATE:
    MAGMA_VIEWPORT:
        MAGMA_VIEWPORT_SCALE:   .half   0,0,0,0
        MAGMA_VIEWPORT_OFFSET:  .half   0,0,0,0
    MAGMA_CLIP_FACTORS:         .half   1,1,MAGMA_DEFAULT_GUARD_BAND,MAGMA_DEFAULT_GUARD_BAND
    MAGMA_VERTEX_SIZE:          .half   0,0,0,0 # vertex_size, MAGMA_VTX_SIZE, MAGMA_VTX_SIZE - vertex_size, vertex_size
    MAGMA_SHADER_TEXT:          .word   0
    MAGMA_SHADER_TEXT_SIZE:     .word   0
    MAGMA_CLIPPING_TEXT:        .word   0
    MAGMA_CLIPPING_TEXT_SIZE:   .word   0
    MAGMA_VERTEX_BUFFER:        .word   0
    MAGMA_TRI_CMD:              .half   0x800
    MAGMA_CULL_MODE:            .byte   0
    MAGMA_OUTPUT_OFFSET:        .byte   0

    .align 4
_MG_VTX_SHADER_UNIFORMS:
.endm

.macro MgEndShaderUniforms
    .align 3
_MG_VTX_SHADER_UNIFORMS_END:
    RSPQ_EndSavedState
.endm

.equ _mg_defining_uniform, 0

.macro MgBeginUniform name binding
    .ifne _mg_defining_uniform
        .error "MgBeginUniform must not be preceded by another MgBeginUniform"
        .exitm
    .endif
    .equ _mg_defining_uniform, 1

    .align 4
\name:
_MG_BINDING_\binding:
.endm

.macro MgEndUniform
    .ifeq _mg_defining_uniform
        .error "MgEndUniform must be preceded by MgBeginUniform"
        .exitm
    .endif
    .equ _mg_defining_uniform, 0
.endm

# Defines the uniform region using the maximum allowable size, but filled with zeroes.
# This is needed to make space for uniforms once in the initial overlay.
# Whenever shaders are loaded, they will be reading/writing from that area automatically.
.macro _MgDefineUniformRegion
    MgBeginShaderUniforms
        .fill MAX_UNIFORMS_SIZE
    MgEndShaderUniforms
.endm

.macro MgBeginShader
    .section .text.magma_vertex_shader
    .align 3
_MG_OVERLAY:

_MG_CLIPPING:
    # Jump to the actual bootstrapper.
    # This piece of code is kept constant, so that _MG_VTX_SHADER is always at the same address.
    j Mg_Clipping
    nop

_MG_VTX_SHADER:
.endm

.macro MgEndShader
    # Place the clipping code bootstrapper in each shader so it can get loaded when required.
Mg_Clipping:
    # Bootstrap the clipping code.
    lw s0, %lo(MAGMA_CLIPPING_TEXT)
    lw t0, %lo(MAGMA_CLIPPING_TEXT_SIZE)
    jal_and_j Mg_LoadOverlay, _MG_CLIPPING
.endm

# Implementation of the shader code bootstrapper
.macro _MgBootstrapShader
    jal_and_j Mg_LoadShader, _MG_VTX_SHADER
.endm


    .data

	RSPQ_BeginOverlayHeader
        RSPQ_DefineCommand MgCmd_SetByte,       8
        RSPQ_DefineCommand MgCmd_SetShort,      8
        RSPQ_DefineCommand MgCmd_SetWord,       8
        RSPQ_DefineCommand MgCmd_SetQuad,       20
        RSPQ_DefineCommand MgCmd_SetShader,     8
        RSPQ_DefineCommand MgCmd_LoadVertices,  8
        RSPQ_DefineCommand MgCmd_DrawIndices,   8
        RSPQ_DefineCommand MgCmd_DrawEnd,       4
        RSPQ_DefineCommand MgCmd_LoadUniform,   8
        RSPQ_DefineCommand MgCmd_LoadUniform,   MAGMA_INLINE_UNIFORM_HEADER + 8
        RSPQ_DefineCommand MgCmd_LoadUniform,   MAGMA_INLINE_UNIFORM_HEADER + 16
        RSPQ_DefineCommand MgCmd_LoadUniform,   MAGMA_INLINE_UNIFORM_HEADER + 32
        RSPQ_DefineCommand MgCmd_LoadUniform,   MAGMA_INLINE_UNIFORM_HEADER + 64
        RSPQ_DefineCommand MgCmd_LoadUniform,   MAGMA_INLINE_UNIFORM_HEADER + 128
        RSPQ_DefineCommand MgCmd_LoadUniform,   248 # TODO: get rid of magic number
	RSPQ_EndOverlayHeader

    .align 4
    .ascii "Dragon RSP Magma"
    .ascii "Code by snacchus"

    .align 4
MAGMA_NORMAL_MASK:              .half   0xF800, 0x07E0, 0x001F, 0, 1, 1<<5, 1<<11, 0
MAGMA_CLIP_CACHE_OFFSETS:       .half   2,4,6,8,10,12,14,16,18

MAGMA_CLIP_PLANES:              
    .half 1, 0, 0,  1
    .half 0, 1, 0,  1
    .half 0, 0, 1,  1
    .half 1, 0, 0, -1
    .half 0, 1, 0, -1
    .half 0, 0, 1, -1

    .bss
    .align 4
MAGMA_CLIP_CACHE_USED:          .dcb.w  16
    .align 3
MAGMA_OUTPUT_BUFFER:            .dcb.b  TRIANGLE_SIZE * 2

MAGMA_CLIP_CACHE:               .dcb.b  MAGMA_VTX_SIZE * MAGMA_CLIP_CACHE_SIZE
MAGMA_CLIP_CACHE_END:

MAGMA_CLIP_LIST0:               .dcb.w  MAGMA_CLIP_CACHE_SIZE
MAGMA_CLIP_LIST1:               .dcb.w  MAGMA_CLIP_CACHE_SIZE

    .text

    #############################################################
    # MgCmd_SetByte
    #############################################################
    .func MgCmd_SetByte
MgCmd_SetByte:
    jr ra
    sb a1, %lo(MAGMA_STATE)(a0)
    .endfunc

    #############################################################
    # MgCmd_SetShort
    #############################################################
    .func MgCmd_SetShort
MgCmd_SetShort:
    jr ra
    sh a1, %lo(MAGMA_STATE)(a0)
    .endfunc

    #############################################################
    # MgCmd_SetWord
    #############################################################
    .func MgCmd_SetWord
MgCmd_SetWord:
    jr ra
    sw a1, %lo(MAGMA_STATE)(a0)
    .endfunc

    #############################################################
    # MgCmd_SetQuad
    #############################################################
    .func MgCmd_SetQuad
MgCmd_SetQuad:
    addi s0, rspq_dmem_buf_ptr, %lo(RSPQ_DMEM_BUFFER)
    lqv $v01, -0x10,s0
    lrv $v01,  0x00,s0
    addi a0, %lo(MAGMA_STATE)
    jr ra
    sqv $v01,  0x00,a0
    .endfunc

    #############################################################
    # MgCmd_LoadUniform
    # ARGS:
    #   a0: RDRAM pointer
    #   a1: Bit 31..16: Size
    #       Bit 15..0: Offset into _MG_VTX_SHADER_UNIFORMS
    #############################################################
    .func MgCmd_LoadUniform
MgCmd_LoadUniform:
    move s0, a0
    addi s4, a1, %lo(_MG_VTX_SHADER_UNIFORMS)
    j DMAIn
    srl t0, a1, 16
    
    .endfunc

    #############################################################
    # MgCmd_SetShader
    # ARGS:
    #   a0: RDRAM pointer to shader ucode text
    #   a1: shader text size
    #############################################################
    .func MgCmd_SetShader
MgCmd_SetShader:
    # TODO: Adjust size of saved data in case the shader uses less DMEM for uniforms.
    #       In theory this could be done by overwriting the state size in the overlay header.
    #       However, this could be problematic because of the profiler buffer that is automatically generated at the end of the state.
    #       There are two potential fixes:
    #       1) Disable this optimization when profiling is enabled.
    #       2) Move the profiler buffer to the beginning of the saved state.
    #       Provided it doesn't break anything, the second option would be preferred.
    sw a0, %lo(MAGMA_SHADER_TEXT)
    sw a1, %lo(MAGMA_SHADER_TEXT_SIZE)
    # fallthrough
    .endfunc

    .func Mg_LoadShader
Mg_LoadShader:
    lw s0, %lo(MAGMA_SHADER_TEXT)
    lw t0, %lo(MAGMA_SHADER_TEXT_SIZE)
    # fallthrough
    .endfunc

    .func Mg_LoadOverlay
Mg_LoadOverlay:
    li s4, %lo(_MG_OVERLAY)
    andi t1, s4, 0xFFF
    add s0, t1
    j DMAIn
    sub t0, t1
    .endfunc

    #############################################################
    # MgCmd_LoadVertices
    # ARGS:
    #   a0: Bit 31..24: Command id
    #       Bit 23..0:  Vertex buffer offset
    #   a1: Bit 31..16: Cache offset
    #       Bit 15..0:  Vertex count
    #############################################################
    .func MgCmd_LoadVertices
MgCmd_LoadVertices:
    # We can't fall through here because RDPQ_Triangle_Send_Async etc. is always placed after the .text segment
    # and we therefore need to put the vertex shader into it's own segment to force it being placed at the end.
    # That means we can't register the command with the shader's address directly because it is not in the .text segment.
    j _MG_VTX_SHADER
    nop
    .endfunc

    #############################################################
    # MgCmd_DrawIndices
    # 
    # ARGS:
    #   a0: Bit 31..24: Command id
    #       Bit 15..0:  Index 0
    #   a1: Bit 31..16: Index 1
    #       Bit 15..0:  Index 2
    #############################################################
    .func MgCmd_DrawIndices
MgCmd_DrawIndices:
    #define vtx1            a0
    #define vtx2            a2
    #define vtx3            a1
    srl a2, a1, 16
    addi s1, rspq_dmem_buf_ptr, %lo(RSPQ_DMEM_BUFFER) - 6
    move s2, s1
    lbu v0, %lo(MAGMA_CULL_MODE)
    
    lbu t7, %lo(MAGMA_OUTPUT_OFFSET)
    addi s3, t7, %lo(MAGMA_OUTPUT_BUFFER)

    li sp, %lo(RSPQ_Loop)
    jal RDPQ_Triangle_Send_Async
    lhu a3, %lo(MAGMA_TRI_CMD)

    xor t7, TRIANGLE_SIZE
    j RSPQ_Loop
    sb t7, %lo(MAGMA_OUTPUT_OFFSET)
    #undef vtx1
    #undef vtx2
    #undef vtx3
    .endfunc

RSPQ_Triangle_Cull:
    jr sp
    nop

    #############################################################
    # MgCmd_DrawEnd
    #############################################################
    .func MgCmd_DrawEnd
MgCmd_DrawEnd:
    j RDPQ_Triangle_Send_End
    sb zero, %lo(MAGMA_OUTPUT_OFFSET)
    .endfunc

#define RDPQ_TRIANGLE_VTX1      a0
#define RDPQ_TRIANGLE_VTX2      a2
#define RDPQ_TRIANGLE_VTX3      a1
#define RDPQ_TRIANGLE_TRICMD    a3
#define RDPQ_TRIANGLE_VTX1_DMEM 0,s1
#define RDPQ_TRIANGLE_VTX2_DMEM 2,s2
#define RDPQ_TRIANGLE_VTX3_DMEM 4,s2
#define RSPQ_Triangle_Clip      _MG_CLIPPING
#include "rsp_rdpq.inc"