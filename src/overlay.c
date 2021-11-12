#include <stdint.h>
#include <malloc.h>
#include "tlb.h"
#include "utils.h"
#include "n64sys.h"
#include "overlayinternal.h"

#define NUM_OVERLAYS       4

overlay_t ovl_desc[16];
uint8_t *ovl_mem;
static int ovl_tlb_idx;

__attribute__((constructor))
void overlay_init(void) {
	extern const uint8_t __ld_ovl0[], __ld_ovl0_end[0];
	extern const uint8_t __ld_ovl1[], __ld_ovl1_end[0];
	extern const uint8_t __ld_ovl2[], __ld_ovl2_end[0];
	extern const uint8_t __ld_ovl3[], __ld_ovl3_end[0];

	const uint8_t *ovl_start[4] = { __ld_ovl0, __ld_ovl1, __ld_ovl2, __ld_ovl3 };
	const uint8_t *ovl_end[4] = { __ld_ovl0_end, __ld_ovl1_end, __ld_ovl2_end, __ld_ovl3_end };

	int max_size = 0;
	for (int i=0; i<4; i++) {
		overlay_t *o = &ovl_desc[i];
		o->rom = ovl_start[i];
		o->size = ovl_end[i] - ovl_start[i];
		if (max_size < o->size)
			max_size = o->size;
	}
	if (max_size == 0)
		return;

	int num_segs = DIVIDE_CEIL(max_size, OVERLAY_SEG_SIZE);

	ovl_mem	= memalign(OVERLAY_SEG_SIZE, num_segs*OVERLAY_SEG_SIZE);
	ovl_tlb_idx = tlb_alloc_indices(num_segs);
}

void __overlay_map_segment(uint32_t vaddr) {
	uint32_t nseg = (vaddr >> OVERLAY_SEG_SHIFT) & (OVERLAY_MAX_SEGMENTS-1);
	uint8_t *phys = ovl_mem + (nseg << OVERLAY_SEG_SHIFT);

	// Invalidate the new portion of memory that's being filled by DMA
	data_cache_hit_invalidate(phys, OVERLAY_SEG_SIZE);
	inst_cache_hit_invalidate(phys, OVERLAY_SEG_SIZE);

	// Map the segment via TLB.
	vaddr &= ~(OVERLAY_SEG_SIZE-1);
	tlb_map_area(ovl_tlb_idx+nseg, vaddr, OVERLAY_SEG_SIZE, phys, false);
}
