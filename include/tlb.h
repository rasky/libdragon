/**
 * @file tlb.c
 * @brief MIPS TLB Interface
 * @ingroup n64sys
 */

#ifndef __LIBDRAGON_TLB_H
#define __LIBDRAGON_TLB_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Reserve a certain number of TLB indices with static allocation.
 * 
 * This function helps  allocating fixed TLB indices across different
 * modules at runtime. TLB indices are divided into two groups: a fixed
 * allocation set (reserved by some modules), and a group of
 * unallocated TLBs that can be used at random (through TLBWR).
 * 
 * Each module that requires a fixed TLB can call tlb_alloc_indices() with the
 * required number of TLB indices. The function will assert if there are not
 * enough indices available.
 * 
 * @param[in]  nidx  The number of indices to allocate.
 *
 * @return     The first index of the allocated group
 */
int tlb_alloc_indices(int nidx);

/**
 * @brief Probe the TLB slots for a certain mapping.
 *
 * This function allows to verify whether a certain address has been
 * already mapped via TLB. It returns the index of the TLB mapping
 * the area, or -1 if not TLB is found.
 *
 * @param[in]  vaddr  The virtual address to probe
 *
 * @return index of the TLB slot mapping the address, or -1 if the address is unmapped.
 */
int tlb_probe(uint32_t vaddr);

/**
 * @brief Check if a virtual address area is fully mapped
 * 
 * This function allows to probe whether a specific virtual memory area is
 * fully mapped. It basically probes the range in increments of 4K to make sure
 * that there are no holes.
 *
 * @param[in]  vaddr  Base virtual address of the area to probe
 * @param[in]  vsize  Size of the virtual area in bytes
 *
 * @return     True if the area is fully mapped by TLBs, False if at least some
 *             parts are not mapped.
 */
bool tlb_is_area_mapped(uint32_t vaddr, uint32_t vsize);

/**
 * @brief Check if a virtual address area is fully unmapped
 * 
 * This function allows to probe whether a specific virtual memory area is
 * fully unmapped. It basically probes the range in increments of 4K to make sure
 * that there are no mapped parts.
 *
 * @param[in]  vaddr  Base virtual address of the area to probe
 * @param[in]  vsize  Size of the virtual area in bytes
 *
 * @return     True if the area is fully not mapped by TLBs, False if at least
 *             one part is mapped.
 */
bool tlb_is_area_unmapped(uint32_t vaddr, uint32_t vsize);

/**
 * @brief Map a memory area via a TLB
 * 
 * This function allows to map a physical memory area to a specific
 * virtual address.
 * 
 * Supported area sizes are all power of twos between 4K and 2048K. Notice
 * that both the virtual and physical address must be aligned to the closest
 * between 4K, 16K, 64K, 256K, or 1024K (eg: a 32K area must be aligned to 16K).
 * 
 * It's not possible to map twice the same virtual area, nor it is possible to
 * map a virtual area that partially overlaps with an already-mapped area. This
 * function has some best-effort internal check and will try to fail if such a
 * double mapping is requested, but it might still create a double mapping which will
 * generate a TLB exception when the area is accessed. You can use
 * #tlb_is_area_unmapped if you are unsure of the mapping status of the area
 * requested.
 *
 * @param[in]  idx        Index of the TLB to use (0-31), or -1 to use a random one
 * @param[in]  vaddr      Virtual address to map
 * @param[in]  vsize      Size of the virtual address area
 * @param[in]  phys       Physical address pointer
 * @param[in]  readwrite  True if the area is read/write, false is read-only
 */
void tlb_map_area(unsigned int idx, uint32_t vaddr, uint32_t vsize, void* phys, bool readwrite);

#endif
