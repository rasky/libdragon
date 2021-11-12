#ifndef __LIBDRAGON_OVERLAYINTERNAL_H
#define __LIBDRAGON_OVERLAYINTERNAL_H

#define OVERLAY_VADDR              0xE0000000
#define OVERLAY_VSIZE              0x01000000

#define OVERLAY_SEG_SHIFT          12
#define OVERLAY_SEG_SIZE           (1<<12)    // 4 Kb
#define OVERLAY_MAX_SEGMENTS       (OVERLAY_VSIZE >> OVERLAY_SEG_SHIFT)

#define OVERLAY_DESC_SIZE_SHIFT    3
#define OVERLAY_DESC_OFF_ROM       0

#ifndef __ASSEMBLER__

#include <stddef.h>

typedef struct {
	const uint8_t* rom;
	int32_t size;
} overlay_t;

overlay_t ovl_desc[16];

_Static_assert((1<<OVERLAY_DESC_SIZE_SHIFT) == sizeof(overlay_t));
_Static_assert(offsetof(overlay_t, rom) == OVERLAY_DESC_OFF_ROM);

#endif

#endif
