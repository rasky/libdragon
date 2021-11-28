/**
 * @file tlb.c
 * @brief MIPS TLB Interface
 * @ingroup n64sys
 * 
 * This file contains the TLB interface.
 */

#include "cop0.h"
#include "debug.h"

/**
 * @brief Initialize the TLB subsystem (called automatically at boot)
 */
__attribute__((constructor))
void __tlb_init(void) {
	// Reset all entries to a non-matching address so that TLB is disabled.
	C0_WRITE_ENTRYHI(0xFFFFFFFF);
	C0_WRITE_ENTRYLO0(0);
	C0_WRITE_ENTRYLO1(0);
	C0_WRITE_PAGEMASK(0);
	for (int i=0;i<32;i++) {
		C0_WRITE_INDEX(i);
		C0_TLBWI();
	}

	// Reset WIRED register to 0. This means that all TLBs are available
	// for random selection, and no fixed TLBs have been allocated.
	C0_WRITE_WIRED(0);
}

int tlb_alloc_indices(int nidx) {
	int wired_idx = C0_WIRED();
	int idx = wired_idx;
	wired_idx += nidx;
	assertf(wired_idx <= 32, "tlb_alloc_indices(%d): not enough TLBs left", nidx);
	C0_WRITE_WIRED(wired_idx);
	return idx;
}

int tlb_probe(uint32_t virt) {
	// Mask off lowest bits. These do not represent the address in ENTRYHI
	// but rather configuration flags (ASID, G), so we ignore them for the probe
	// for now.
	virt &= ~0x1FFF;

	// Do a TLB probe on the specified address
	C0_WRITE_ENTRYHI(virt);
	C0_TLBP();

	// Return the index, or -1 if the probe failed
	int index = C0_INDEX();
	if (index & C0_INDEX_PROBE_FAILED)
		return -1;
	return index;
}

bool tlb_is_area_mapped(uint32_t vaddr, uint32_t vsize) {
	for (int off = 0; off < vsize; off += 4096)
		if (tlb_probe(vaddr + off) < 0)
			return false;
	return tlb_probe(vaddr + vsize - 1) >= 0;
}

bool tlb_is_area_unmapped(uint32_t vaddr, uint32_t vsize) {
	for (int off = 0; off < vsize; off += 4096)
		if (tlb_probe(vaddr + off) >= 0)
			return false;
	return tlb_probe(vaddr + vsize - 1) < 0;
}

void tlb_map_area(unsigned int idx, uint32_t virt, uint32_t vsize, void* phys, bool readwrite) {
	uint32_t vmask = vsize-1;

	// Configure the COP0 PAGEMASK register depending on the specified virtual
	// memory area. Notice that we also play with the fact that a TLB is a double
	// mapping (ood/even pages) to allow intermediate sizes that wouldn't be
	// allowed.
	bool dbl;
	switch (vmask) {
	case 0x000FFF: C0_WRITE_PAGEMASK(0x00 << 13); dbl=false; break;
	case 0x001FFF: C0_WRITE_PAGEMASK(0x00 << 13); dbl=true;  break;
	case 0x003FFF: C0_WRITE_PAGEMASK(0x03 << 13); dbl=false; break;
	case 0x007FFF: C0_WRITE_PAGEMASK(0x03 << 13); dbl=true;  break;
	case 0x00FFFF: C0_WRITE_PAGEMASK(0x0F << 13); dbl=false; break;
	case 0x01FFFF: C0_WRITE_PAGEMASK(0x0F << 13); dbl=true;  break;
	case 0x03FFFF: C0_WRITE_PAGEMASK(0x3F << 13); dbl=false; break;
	case 0x07FFFF: C0_WRITE_PAGEMASK(0x3F << 13); dbl=true;  break;
	case 0x0FFFFF: C0_WRITE_PAGEMASK(0xFF << 13); dbl=false; break;
	case 0x1FFFFF: C0_WRITE_PAGEMASK(0xFF << 13); dbl=true;  break;
	default: assertf(0, "unsupported virtual area size in tlb_map_area: %lx", vsize); return;
	}

	// Check whether the addresses are correctly aligned
	if (dbl) vmask >>= 1;
	assertf(((uint32_t)phys & vmask) == 0, "physical address %p is not aligned to %lu (0x%lx) bytes", phys, vmask+1, vmask+1);
	assertf((virt & vmask) == 0, "virtual address %lx is not aligned to %lu (0x%lx) bytes", virt, vmask+1, vmask+1);

	// Compute real virtual address (including the double-area trick) and write
	// it into the ENTRYHI register.
	uint32_t vpn2 = virt;
	if (!dbl) vpn2 &= ~(vmask+1);
	C0_WRITE_ENTRYHI(vpn2);

	// Probe the TLB, to check whether this area was already registered, so to
	// give a proper error message This is just a best-effort check on the initial
	// address as it costs close to nothing; the user is expected to call us
	// with an unmapped area, or use the slower #tlb_is_area_unmapped themselves.
	C0_TLBP();
	uint32_t existing = C0_INDEX();
	uint32_t exentry0 = C0_ENTRYLO0();
	uint32_t exentry1 = C0_ENTRYLO1();
	assertf((existing & C0_INDEX_PROBE_FAILED) || (((exentry0|exentry1) & 2) == 0),
		"duplicated TLB entry with vaddr %08lx (%lx/%lx)", vpn2, exentry0, exentry1);

	// Prepare the ENTRYLO/ENTRYLO1 registers with the physical location pointer.
	// Make all the mappings global (we don't support per-thread TLB at the moment).
	if (dbl || !(virt & (vmask+1))) {	
		uint32_t entry = ((uint32_t)phys & 0x3FFFFFFF) >> 6;
		if (readwrite) entry |= C0_ENTRYLO_DIRTY;
		entry |= C0_ENTRYLO_VALID | C0_ENTRYLO_GLOBAL;
		C0_WRITE_ENTRYLO0(entry);
	} else {
		C0_WRITE_ENTRYLO0(C0_ENTRYLO_GLOBAL);
	}

	if (dbl || (virt & (vmask+1))) {	
		uint32_t entry = (((uint32_t)phys + (dbl ? vmask + 1 : 0)) & 0x3FFFFFFF) >> 6;
		if (readwrite) entry |= C0_ENTRYLO_DIRTY;
		entry |= C0_ENTRYLO_VALID | C0_ENTRYLO_GLOBAL;
		C0_WRITE_ENTRYLO1(entry);
	} else {
		C0_WRITE_ENTRYLO1(C0_ENTRYLO_GLOBAL);		
	}

	// Write the TLB
	if (idx == -1) {
		C0_TLBWR();	
	} else {	
		assertf(idx >= 0 && idx < 32, "invalid TLB index: %d", idx);
		C0_WRITE_INDEX(idx);
		C0_TLBWI();
	}
}
