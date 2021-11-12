#include "regs.S"
#include "overlayinternal.h"

#define C0_BADVADDR    $8

#define PI_REGS_BASE   0xA4600000
#define PI_REG_RAM     0x00
#define PI_REG_ROM     0x04
#define PI_REG_WLEN    0x0C
#define PI_REG_STATUS  0x10

	.set noreorder
	.set noat

	.global ovl_intvector
ovl_intvector:
	la k1, ovl_tlbhandler
	jr k1
	nop

	#######################################
	# TLB Miss exception handler
	#######################################

	.global ovl_tlbhandler
ovl_tlbhandler:
	# Check if the missing address falls in the 0xExxxxxxx range, which is
	# reserved for overlays.
	mfc0 k1, C0_BADVADDR
	sd t0, -8(sp)
	sd t1, -16(sp)
	srl k0, k1, 28
	xori k0, OVERLAY_VADDR>>28
	bnez k0, ovl_fallback
	srl k0, k1, 24
	andi k0, 0xF

	# Now k0 is the overlay index. Fetch the ROM pointer. If NULL, this overlay
	# does not exist and we can fallback to the default handler
	la t0, ovl_desc
	sll k0, OVERLAY_DESC_SIZE_SHIFT
	add t0, k0
	lw t0, OVERLAY_DESC_OFF_ROM(t0)
	beqz t0, ovl_fallback

	# Extract segment number
	srl t1, k1, OVERLAY_SEG_SHIFT
	andi t1, OVERLAY_MAX_SEGMENTS-1

	# Start PI DMA transfer. First wait until it is not busy
	# TODO: we could try to force-break a running DMA here, and restore it
	# later when we are done.
	li k1, PI_REGS_BASE
1:
	lw k1, PI_REG_STATUS(k1)
	andi k1, 3
	bnez k1, 1b
	li k1, PI_REGS_BASE

	# Calculate segment ROM address and store it into PI DMA
	add t0, t1
	sw t0, PI_REG_ROM(k1)

	# Calculate RAM address
	lw t0, ovl_mem
	add t0, t1
	sw t0, PI_REG_RAM(k1)

	# Store length and start transfer
	li t0, OVERLAY_SEG_SIZE-1
	sw t0, PI_REG_WLEN(k1)

	# We're now fetching the overlay portion from ROM. We have to wait
	# completion before returning so it's a good time to switch to C
	# for doing the TLB mapping.

	# Save all caller-saved registers. We need to save 21 registers more,
	# plus 2 that were already saved (t0, t1)
	addi sp, -23*8
	# Keep slots 0-3 empty as argument slots for function calls.
	# This is required by MIPS ABI even if we only call functions
	# that receive arguments in registers.
	sd $1, 4*8(sp)
	sd v0, 5*8(sp)
	sd v1, 6*8(sp)
	sd a0, 7*8(sp)
	sd a1, 8*8(sp)
	sd a2, 9*8(sp)
	sd a3, 10*8(sp)
	sd t2, 11*8(sp)
	sd t3, 12*8(sp)
	sd t4, 13*8(sp)
	sd t5, 14*8(sp)
	sd t6, 15*8(sp)
	sd t7, 16*8(sp)
	sd t8, 17*8(sp)
	sd t9, 18*8(sp)
	sd ra, 19*8(sp)
	mflo t0
	mfhi t1
	sd t0, 20*8(sp)
	sd t1, 21*8(sp)
	.set at          # we can use assembly macros now to simplify our job

	# TODO: We don't save FPU registers. We could disable FPU in COP0 STATUS,
	# but that requires exceptions to be enabled for it to work. We should 
	# experiment reenabling exceptions here (not interrupts).
	jal __overlay_map_segment
	mfc0 a0, C0_BADVADDR

	ld t0, 20*8(sp)
	ld t1, 21*8(sp)
	mtlo t0
	mthi t1

	.set noat    # no more assembly macros from now on
	ld $1, 4*8(sp)
	ld v0, 5*8(sp)
	ld v1, 6*8(sp)
	ld a0, 7*8(sp)
	ld a1, 8*8(sp)
	ld a2, 9*8(sp)
	ld a3, 10*8(sp)
	ld t2, 11*8(sp)
	ld t3, 12*8(sp)
	ld t4, 13*8(sp)
	ld t5, 14*8(sp)
	ld t6, 15*8(sp)
	ld t7, 16*8(sp)
	ld t8, 17*8(sp)
	ld t9, 18*8(sp)
	ld ra, 19*8(sp)
	addiu sp, 23*8

	ld t0, -8(sp)
	ld t1, -16(sp)
	eret

ovl_fallback:
	ld t0, -8(sp)
	j inthandler
	ld t1, -16(sp)
