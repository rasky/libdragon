#ifndef SFX64_H
#define SFX64_H

#include "mixer.h"

typedef struct {
	waveform_t wave;
	uint32_t rom_addr;
} sfx64_t;

void sfx64_open(sfx64_t *sfx, const char *fn);
void sfx64_set_frequency(sfx64_t *sfx, uint32_t frequency);
void sfx64_set_loop(sfx64_t *sfx, bool loop);


#endif

