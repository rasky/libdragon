#include <regdef.h>
#include "rsp.inc"
#include "rdp.inc"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define VIDEO_WIDTH 320
#define VIDEO_HEIGHT 240

.macro DMACopyFromRDRAM start:req,len:req,dest:req
    .set noat
    mtc0 \start, COP0_DMA_RAMADDR
    li $1, (\dest) & 0x1FFF
    mtc0 $1, COP0_DMA_SPADDR
    li $1, (\len)-1
    mtc0 $1, COP0_DMA_READ
    .set at
.endm

.macro DMAStridedCopyFromRDRAM start:req,len:req,dest:req,count:req,skip:req
    DMACopyFromRDRAM \start, (\len) | (((\count)-1)<<12) | ((\skip)<<20), \dest
.endm

.macro DMAWait
    .set noat
1:  mfc0 $1,COP0_SP_STATUS
    andi $1,(1<<2)|(1<<3)   # BUSY / FULL
    bnez $1, 1b
    nop
    .set at
.endm

.macro DMACopyToRDRAM start:req,len:req,dest:req
    .set noat
    mtc0 \start, COP0_DMA_RAMADDR
    mtc0 \dest, COP0_DMA_SPADDR
    li $1, (\len)-1
    mtc0 $1, COP0_DMA_WRITE
    .set at
.endm

.macro DMAStridedCopyToRDRAM start:req,len:req,dest:req,count:req,skip:req
    DMACopyToRDRAM \start, (\len) | (((\count)-1)<<12) | ((\skip)<<20), \dest
.endm

.set noreorder

# Size of a "block" of the picture that is processed on each iteration.
# 32x16 is a good enough size that won't overflow the 4K internal DMEM and
# allows enough parallelization of pixel processing.
#define BLOCK_W 32
#define BLOCK_H 16
#define BLOCK_LEN (BLOCK_W*BLOCK_H)

# Layout of data segment: input buffer
#define INPUT_BASE_ADDR     256
#define INPUT_Y             INPUT_BASE_ADDR
#define INPUT_CB            INPUT_Y + BLOCK_LEN
#define INPUT_CR            INPUT_CB + BLOCK_LEN/4
#define INPUT_END           INPUT_CR + BLOCK_LEN/4

# Layout of data segment: output buffers (we keep two buffers to do double-buffering)
#define OUTPUT_BASE_ADDR    2048
#define OUTPUT_BUF0         OUTPUT_BASE_ADDR
#define OUTPUT_BUF1         OUTPUT_BUF0 + BLOCK_LEN*2
#define OUTPUT_END          OUTPUT_BUF1 + BLOCK_LEN*2

#if OUTPUT_BASE_ADDR < INPUT_END
#error Invalid memory layout
#endif

#if OUTPUT_END > 4096
#error Invalid memory layout
#endif


    .data

# Input arguments (filled by CPU)
COMMAND_OUTBUF:         .long 0    # Output buffer in RDRAM (where the interleaved frame will be drawn)
COMMAND_YBUF:           .long 0    # Input buffer of Y component (VIDEO_WIDTH x VIDEO_HEIGHT)
COMMAND_CRBUF:          .long 0    # Input buffer of subsampled Cr component (VIDEO_WIDTH/2 x VIDEO_HEIGHT/2)
COMMAND_CBBUF:          .long 0    # Input buffer of subsampled Cb component (VIDEO_WIDTH/2 x VIDEO_HEIGHT/2)
COMMAND_RDP_DL_START:   .long 0    # Address in RDRAM of RDP display list to trigger when finished
COMMAND_RDP_DL_END:     .long 0    # Address in RDRAM of RDP display list end

    .align 4

v1temp: .quad 0
        .quad 0
        .quad 0
        .quad 0
v2temp: .quad 0
        .quad 0
        .quad 0
        .quad 0

    # Initialize VSHL8/VSHR calculations
    vsll8_data

    .align 4
BANNER0:    .ascii "RSPH264 by Rasky"
BANNER1:    .ascii "Mod: YUV blitter"

    .text

.globl _start
_start:
    setup_vsll8 $v30

YUVDraw:
    #define outbuf  s0
    #define ybuf    s1
    #define crbuf   s2
    #define cbbuf   s3
    #define row     s4
    #define col     s5
    #define block   s6

    lw outbuf, %lo(COMMAND_OUTBUF)
    lw ybuf, %lo(COMMAND_YBUF)
    lw crbuf, %lo(COMMAND_CRBUF)
    lw cbbuf, %lo(COMMAND_CBBUF)

    li block, 0
    li row, 0
    li k0, OUTPUT_BUF1

    LoopBlock:
        li col, 0

        LoopBlockCol:
            # DMA Y/CR/CB blocks from RDRAM into DMEM
            DMAStridedCopyFromRDRAM ybuf, BLOCK_W, INPUT_Y, BLOCK_H, VIDEO_WIDTH-BLOCK_W
            DMAWait

            DMAStridedCopyFromRDRAM cbbuf, BLOCK_W/2, INPUT_CB, BLOCK_H/2, (VIDEO_WIDTH-BLOCK_W)/2
            DMAWait

            DMAStridedCopyFromRDRAM crbuf, BLOCK_W/2, INPUT_CR, BLOCK_H/2, (VIDEO_WIDTH-BLOCK_W)/2
            DMAWait

            # First block: we do not have an output buffer ready.
            beqz block, noout
            nop
            # Start copying the output buffer to RDRAM in background, while next block is being
            # processed on the other buffer.
            DMAStridedCopyToRDRAM outbuf, BLOCK_W*2, k0, BLOCK_H, (VIDEO_WIDTH-BLOCK_W)*2
            add outbuf, k1
            xori k0, 1024  # swap buffer pointer
        noout:

            # Interleave the current block
            li a0, INPUT_Y
            li a1, INPUT_CB
            li a2, INPUT_CR
            #if BLOCK_W == 16
            jal YUVInterleaveBlock16
            #elif BLOCK_W == 32
            jal YUVInterleaveBlock32
            #else
            #error Unsupported block width
            #endif
            move a3, k0

            # Increment pointers
            addi ybuf, BLOCK_W
            addi crbuf, BLOCK_W/2
            addi cbbuf, BLOCK_W/2
            li k1, BLOCK_W*2   # save increment to the output buffer for later

            # Wait for the DMA copy of the output buffer.
            DMAWait

            addi col, BLOCK_W
            bne col, VIDEO_WIDTH, LoopBlockCol
            addi block, 1

        # Increment pointers to the start of the next row of blocks. We need
        # to skip BLOCK_H-1 lines.
        li k1, (VIDEO_WIDTH*(BLOCK_H-1))*2 + BLOCK_W*2
        addi ybuf, (VIDEO_WIDTH*(BLOCK_H-1))
        addi crbuf, (VIDEO_WIDTH*(BLOCK_H/2-1))/2
        addi cbbuf, (VIDEO_WIDTH*(BLOCK_H/2-1))/2

        addi row, BLOCK_H
        bne row, VIDEO_HEIGHT, LoopBlock
        nop

    # Copy the last block to RDRAM
    DMAStridedCopyToRDRAM outbuf, BLOCK_W*2, k0, BLOCK_H, (VIDEO_WIDTH-BLOCK_W)*2
    DMAWait

    #undef outbuf
    #undef ybuf
    #undef crbuf
    #undef cbbuf
    #undef row
    #undef col

    # Tell the RDP to begin drawing the display list stored in RDRAM
    lw t0, %lo(COMMAND_RDP_DL_START)
    lw t1, %lo(COMMAND_RDP_DL_END)
    RdpSendReg t0, t1

    break


# Function YUVInterleaveBlock32(ybuf, cbbuf, crbuf, outbuf)
#
# Optimized interleaving for BLOCK_W==32. 
YUVInterleaveBlock32:
    #define ybuf   a0
    #define cbbuf  a1
    #define crbuf  a2
    #define outbuf a3

    #define y       t8

    li y, BLOCK_H/2-1
    li t1, %lo(v1temp)
    li t2, %lo(v2temp)

#if BLOCK_W != 32
    break
#endif

    # Interleave 2 lines of 32 horizontal pixels each loop. The code
    # has been laid out to take maximum advantage of parallel VU/SU execution
    # of opcodes, taking also into account instruction latency to avoid
    # pipeline stalls (4 ops for memory accesses and 3 ops for each
    # vector instruction).
    #
    # This optimized loop takes 34 cycles to interleave 64 pixels.
    .align 3 
    InterleaveLoopRow2:
                                            # Read CB/CR elements
                                            lpv $v15,0,  0*8,crbuf # CR<<8 (first half)           # 1
                                            lpv $v17,0,  1*8,crbuf # CR<<8 (second half)          # 2
                                            lpv $v14,0,  0*8,cbbuf # CB<<8 (first half)           # 3
                                            lpv $v16,0,  1*8,cbbuf # CB<<8 (second half)          # 4

        # Calculate (CB<<8)|CR              # Read Y buffer elements
        vsrl $v15, $v15, 8;                 lpv $v00,0,  0*8,ybuf  # row 0, first quarter         # 5
        vsrl $v17, $v17, 8;                 lpv $v01,0,  1*8,ybuf  # row 0, second quarter        # 6
        
                                            lpv $v08,0,  4*8,ybuf  # row 1, first quarter         # 7
        vor $v15, $v15, $v14,0;             lpv $v09,0,  5*8,ybuf  # row 1, second quarter        # 8
        vor $v17, $v17, $v16,0;             lpv $v02,0,  2*8,ybuf  # row 0, third quarter         # 9
                                            lpv $v03,0,  3*8,ybuf  # row 0, fourth quarter        # 10

        # Shift Y buffer elements right     # Store interleaved CB/CR into t1/t2
        # to go into the lower byte of      # and finish reading Y buffer
        # each lane of vregs.
        vsrl $v00, $v00, 8;                 sqv $v15,0,  0*16,t1                                   # 11,12 (stall: store 2 cycles after load)
        vsrl $v01, $v01, 8;                 sqv $v17,0,  0*16,t2                                   # 13
        vsrl $v08, $v08, 8;                 lpv $v10,0,  6*8,ybuf  # row 1, third quarter         # 14
        vsrl $v09, $v09, 8;                 lpv $v11,0,  7*8,ybuf  # row 1, fourth quarter        # 15

                                            # Read back CB/CR interleaved values
        vsrl $v02, $v02, 8;                 lpv $v14,0,  0*8,t1                                   # 16
        vsrl $v03, $v03, 8;                 lpv $v15,0,  1*8,t1                                   # 17
        vsrl $v10, $v10, 8;                 lpv $v16,0,  0*8,t2                                   # 18
        vsrl $v11, $v11, 8;                 lpv $v17,0,  1*8,t2                                   # 19

        # Compose Y with CB/Cr              # Prepare pointers for next loop
        vor $v00, $v00, $v14,0;             addi crbuf, 8*2                                     # 20
        vor $v01, $v01, $v15,0;             addi cbbuf, 8*2                                     # 21
        vor $v08, $v08, $v14,0;             addi ybuf, 32*2                                     # 22
        vor $v09, $v09, $v15,0;             addi outbuf, 64*2                                   # 23

                                            # Write interleaved pixels in output
                                            # buffer.
        vor $v02, $v02, $v16,0;             sqv $v00,0,  -8*16,outbuf                              # 24
        vor $v03, $v03, $v17,0;             sqv $v01,0,  -7*16,outbuf                              # 25
        vor $v10, $v10, $v16,0;             sqv $v08,0,  -4*16,outbuf                              # 26
        vor $v11, $v11, $v17,0;             sqv $v09,0,  -3*16,outbuf                              # 27

                                            sqv $v02,0,  -6*16,outbuf                              # 28
                                            sqv $v03,0,  -5*16,outbuf                              # 29
                                            sqv $v10,0,  -2*16,outbuf                              # 30
                                            sqv $v11,0,  -1*16,outbuf                              # 31

        bnez y, InterleaveLoopRow2                                                              # 32
        addi y, -1                                                                              # 33,34 (stall: taken branch)

    jr ra
    nop

    #undef ybuf
    #undef crbuf
    #undef cbbuf
    #undef outbuf
    #undef y



# Function YUVInterleaveBlock16(ybuf, cbbuf, crbuf, outbuf)
#
# Optimized interleaving for BLOCK_W==16. 
# Not used anymore, only kept as "reference implementation".
#
YUVInterleaveBlock16:
    #define ybuf   a0
    #define cbbuf  a1
    #define crbuf  a2
    #define outbuf a3

    #define y       t8

#if BLOCK_W != 16
    break
#endif

    li y, BLOCK_H/2-1
    li t1, %lo(v1temp)

    InterleaveLoopRow:
        # First, interleave CB/CR
        lpv $v15,0,  0x00*8,crbuf   # v15 = CR<<8
        lpv $v14,0,  0x00*8,cbbuf   # v14 = CB<<8
        lpv $v00,0,  0x00*8,ybuf  # row 0, first half
        lpv $v01,0,  0x01*8,ybuf  # row 0, second half
        lpv $v08,0,  0x02*8,ybuf  # row 1, first half
        lpv $v09,0,  0x03*8,ybuf  # row 2, second half

        # Calculate (CB<<8)|CR
        vsrl $v15, $v15, 8
        vor $v15, $v15, $v14,0
        sqv $v15,0,  0x00*16,t1

        # Now load back CB/CR as separate lanes, so they are ready to
        # be interleaved with Y
        lpv $v14,0,  0x00*8,t1
        lpv $v15,0,  0x01*8,t1

        vsrl $v00, $v00, 8
        vsrl $v01, $v01, 8
        vsrl $v08, $v08, 8
        vsrl $v09, $v09, 8

        vor $v00, $v00, $v14,0
        vor $v01, $v01, $v15,0
        vor $v08, $v08, $v14,0
        vor $v09, $v09, $v15,0

        sqv $v00,0,  0x00*16,outbuf
        addi crbuf, 8
        sqv $v01,0,  0x01*16,outbuf
        addi cbbuf, 8
        sqv $v08,0,  0x02*16,outbuf
        addi ybuf, 32
        sqv $v09,0,  0x03*16,outbuf
        addi outbuf, 64

        bnez y, InterleaveLoopRow
        addi y, -1

    jr ra
    nop

    #undef ybuf
    #undef crbuf
    #undef cbbuf
    #undef outbuf
    #undef y
