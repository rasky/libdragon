#include <libdragon.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define SFX64_FILE_VERSION  1


void raw64_decoder_read(samplebuffer_t *sbuf, int base_rom_addr, int wpos, int wlen, int bps) {
	// debugf("raw64_decoder_read: rom:%x wpos:%x wlen:%x\n", base_rom_addr, wpos, wlen);

	if (bps==0 && (wlen&1)) wlen++;

	uint32_t rom_addr = base_rom_addr + (wpos << bps);
	uint8_t* buf = (uint8_t*)samplebuffer_append(sbuf, wlen);

	int bytes = wlen << bps;
	uint8_t *ram_addr = buf;
	uint32_t misalign = (uint32_t)ram_addr & 7;
	if (misalign) {
		ram_addr -= misalign; buf -= misalign;
		rom_addr -= misalign;
		bytes += misalign;
	}
	if (rom_addr & 1) {
		rom_addr++; // FIXME
	}

	// data_cache_hit_writeback_invalidate(buf-2, bytes+2);

	dma_read(ram_addr, rom_addr, bytes);

	// debugf("DMA: [%02x%02x]%02x%02x%02x%02x...%02x%02x%02x%02x\n",
	// 	buf[-2], buf[-1], buf[0], buf[1], buf[2], buf[3],
	// 	buf[bytes-4], buf[bytes-3], buf[bytes-2], buf[bytes-1]);
}

static void decoder_read(void *ctx, samplebuffer_t *sbuf, int wpos, int wlen, bool seeking) {
	sfx64_t *sfx = (sfx64_t*)ctx;

	// debugf("sfx64_decoder_read: wpos:%x wlen:%x\n", wpos, wlen);
	raw64_decoder_read(sbuf, sfx->rom_addr, wpos, wlen, 0);
}
void sfx64_open(sfx64_t *sfx, const char *fn) {
	memset(sfx, 0, sizeof(*sfx));

	sfx->wave.name = fn;
	sfx->wave.nbits = 8;
	sfx->wave.frequency = 44100;
	sfx->wave.loop_len = 0; 

	int fh = dfs_open(fn);
	assertf(fh >= 0, "file does not exist: %s", fn);
	sfx->wave.len = dfs_size(fh);
	dfs_close(fh);

	sfx->rom_addr = dfs_rom_addr(fn);
	assertf(sfx->rom_addr != 0, "file does not exist: %s", fn);

	sfx->wave.read = decoder_read;
	sfx->wave.ctx = sfx;
}

void sfx64_set_frequency(sfx64_t *sfx, uint32_t frequency) {
	sfx->wave.frequency = frequency;
}

void sfx64_set_loop(sfx64_t *sfx, bool loop) {
	sfx->wave.loop_len = sfx->wave.len;
}
