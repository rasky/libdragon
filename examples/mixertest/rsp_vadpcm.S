#include <rsp.inc>

#define MAX_CHUNKS 64

	.data

ARG_INPUT:	     .long 0
ARG_OUTPUT:      .long 0
ARG_NUMCHUNKS:   .long 0

	.bss

	.align 4
INPUT:   .dcb.b   MAX_CHUNKS*9

	.align 4
OUTPUT:  .dcb.w   MAX_CHUNKS*16

	.text

	#define num_chunks   k0

start:
	lw num_chunks, %lo(ARG_NUMCHUNKS)

	# Compute number of bytes for the chunks (*9)
	sll a2, num_chunks, 3
	add a2, num_chunks

	addi t0, a2, 6    # round up for DMA (would be +7 and then -1 for DMA_SIZE macro)
	lw s0, %lo(ARG_INPUT)
	jal DMAIn
	li s4, %lo(INPUT)

	move s0, s4       # use DMA-alignmented-adjusted input buffer
	jal Decompress
	li s4, %lo(OUTPUT)

	lw num_chunks, %lo(ARG_NUMCHUNKS)
	sll t0, num_chunks, 4
	lw s0, %lo(ARG_OUTPUT)
	li s4, %lo(OUTPUT)
	jal DMAOut
	addi t0, -1   # adjust as DMA_SIZE macro

	break


	.func Decompress
Decompress:
	# s0 -> INPUT
	# s4 -> OUTPUT
	# a2 -> num bytes of input buffer
	# num_chunks -> number of compressed chunks (a2/9)

	vxor $v00, $v00, $v00,0

	# FIXME: Just zero the buffer for now...
decloop:
	# 16 16-bit samples into s4
	sqv $v00,0, 0,s4
	sqv $v00,0, 1,s4

	add s4, 32
	addi num_chunks, -1
	bgtz num_chunks, decloop
	nop

	jr ra
	nop

	.endfunc


#include <rsp_dma.inc>