#include "libdragon.h"
#include "regsinternal.h"
#include "mixer.h"
#include <memory.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#define MIXER_TRACE   0

#if MIXER_TRACE
#define tracef(fmt, ...)  debugf(fmt, ##__VA_ARGS__)
#else
#define tracef(fmt, ...)  ({ })
#endif

static volatile struct AI_regs_s * const AI_regs = (struct AI_regs_s *)0xa4500000;

/**
 * @name AI Status Register Values
 * @{
 */
/** @brief Bit representing that the AI is busy */
#define AI_STATUS_BUSY  ( 1 << 30 )
/** @brief Bit representing that the AI is full */
#define AI_STATUS_FULL  ( 1 << 31 )
/** @} */

#define MAX_EVENTS              32
#define MIXER_POLL_PER_SECOND   8

/**
 * RSP mixer ucode
 */
DEFINE_RSP_UCODE(rsp_mixer);


/**
 * Number of bytes in sample buffers that must be over-read to make the
 * RSP ucode safe.
 * 
 * RSP ucode doesn't currently bound check sample buffer accesses for
 * performance reasons (and missing implementation). In case of loops,
 * this means that the RSP will go beyond the loop end point, before
 * looping, up to 64 bytes (which is the internal DMEM buffer, 
 * MEM_SAMPLE_CACHE).
 * 
 * So in general, when playing a looping waveform, the mixer will need
 * to repeat the loop start after the loop end for up to 64 bytes.
 */
#define MIXER_LOOP_OVERREAD     64

#define MIN(a,b)  ({ typeof(a) _a = a; typeof(b) _b = b; _a < _b ? _a : _b; })
#define ROUND_UP(n, d)  (((n) + (d) - 1) / (d) * (d))

#define CH_FLAGS_BPS_SHIFT  (3<<0)   // BPS shift value

// Fixed point value used in waveform position calculations. This is a signed
// 32-bit integer with the fractional part using MIXER_FX32_FRAC bits.
// You can use MIXER_FX32() to convert from float.
typedef int32_t mixer_fx32_t;

// Fixed point value used for volume and panning calculations.
// You can use MIXER_FX15() to convert from float.
typedef int16_t mixer_fx15_t;


#define MIXER_FX32_FRAC    12    // NOTE: this must be the same of WAVERFORM_POS_FRAC_BITS in rsp_mixer.S
#define MIXER_FX32(f)      (int32_t)((f) * (1<<MIXER_FX32_FRAC))

#define MIXER_FX15_FRAC    15
#define MIXER_FX15(f)      (int16_t)((f) * ((1<<MIXER_FX15_FRAC)-1))

#define MIXER_FX16_FRAC    16
#define MIXER_FX16(f)      (int16_t)((f) * ((1<<MIXER_FX16_FRAC)-1))

typedef struct mixer_channel_s {
	/* Current position within the waveform (in bytes) */
	mixer_fx32_t pos;
	/* Step between samples (in bytes) to playback at the correct frequency */
	mixer_fx32_t step;
	/* Length of the waveform (in bytes) */
	mixer_fx32_t len;
	/* Length of the loop in the waveform (in bytes) */
	mixer_fx32_t loop_len;
	/* Pointer to the waveform */
	void *ptr;
	/* Misc flags */
	uint32_t flags;
} mixer_channel_t;

_Static_assert(sizeof(mixer_channel_t) == 6*4);

typedef struct {
	int16_t left;
	int16_t right;
} mixer_sample_t;

_Static_assert(sizeof(mixer_sample_t) == 4);

typedef struct {
	int max_bits;
	float max_frequency;
	int max_buf_sz;
} channel_limit_t;

typedef struct {
	int64_t ticks;
	MixerEvent cb;
	void *ctx;
} mixer_event_t;

struct {
	uint32_t sample_rate;
	int num_channels;
	float divider;
	float vol;


	mixer_sample_t *buffer;
	int buf_size;
	volatile int buf_r, buf_w, buf_w2;

	int64_t ticks;
	int num_events;
	mixer_event_t events[MAX_EVENTS];

	uint8_t *ch_buf_mem;
	samplebuffer_t ch_buf[MIXER_MAX_CHANNELS];
	channel_limit_t limits[MIXER_MAX_CHANNELS];

	mixer_channel_t channels[MIXER_MAX_CHANNELS] __attribute__((aligned(8)));
	mixer_fx15_t lvol[MIXER_MAX_CHANNELS];
	mixer_fx15_t rvol[MIXER_MAX_CHANNELS];

	// Permanent state of the ucode across different executions
	uint8_t ucode_state[128] __attribute__((aligned(8)));

} Mixer;

static inline int mixer_initialized(void) { return Mixer.num_channels != 0; }

static void mixer_interrupt(void) {
	assert(!(AI_regs->status & AI_STATUS_FULL));

	if (Mixer.buf_w == Mixer.buf_r) {
		if (Mixer.buf_w2 == 0) {
			return;
		}
		Mixer.buf_r = 0;
		Mixer.buf_w = Mixer.buf_w2;
		Mixer.buf_w2 = 0;		
	}
	assert(Mixer.buf_w > Mixer.buf_r);

	int nbytes = (Mixer.buf_w - Mixer.buf_r) * 4;
	assert((nbytes & 7) == 0);

	AI_regs->address = UncachedAddr(Mixer.buffer + Mixer.buf_r);
	AI_regs->length = nbytes;
	AI_regs->control = 1;
	Mixer.buf_r = Mixer.buf_w;

	if (!(AI_regs->status & AI_STATUS_FULL))
		mixer_interrupt();
}


void mixer_init(int num_channels, int sample_rate) {
	extern void audio_dac_init(int frequency);

	memset(&Mixer, 0, sizeof(Mixer));
	audio_dac_init(sample_rate);

	Mixer.num_channels = num_channels;
	Mixer.sample_rate = audio_get_frequency();  // actual sample rate obtained via DAC clock
	Mixer.vol = 1.0f;

	// Allocate the audio buffer
	enum { BUF_SECOND_FRAC = 6 };
	Mixer.buf_size = ROUND_UP(Mixer.sample_rate / BUF_SECOND_FRAC, 8);
	Mixer.buffer = malloc(Mixer.buf_size * sizeof(mixer_sample_t));
	assert(((uint32_t)Mixer.buffer & 7) == 0);
	data_cache_hit_writeback_invalidate(Mixer.buffer, Mixer.buf_size*sizeof(mixer_sample_t));

	for (int ch=0;ch<MIXER_MAX_CHANNELS;ch++) {
		mixer_ch_set_vol(ch, 1.0f, 1.0f);
		mixer_ch_set_limits(ch, 16, Mixer.sample_rate, 0);
	}

	register_AI_handler(mixer_interrupt);
	set_AI_interrupt(1);
}

static void mixer_init_samplebuffers(void) {
	// Initialize the samplebuffers. This is done lazily so to allow the
	// client to configure the limits of the channels.
	int totsize = 0;
	int bufsize[32];

	for (int i=0;i<Mixer.num_channels;i++) {
		// Get maximum frequency for this channel: (default: mixer sample rate)
		int nsamples = Mixer.limits[i].max_frequency;
		if (!nsamples)
			nsamples = Mixer.sample_rate;

		// Multiple by maximum byte per sample
		nsamples *= Mixer.limits[i].max_bits == 8 ? 1 : 2;

		// Calculate buffer size according to number of expected polls per second.
		bufsize[i] = ROUND_UP((int)ceilf((float)nsamples / (float)MIXER_POLL_PER_SECOND), 8);

		// If we're over the allowed maximum, clamp to it
		if (Mixer.limits[i].max_buf_sz && bufsize[i] > Mixer.limits[i].max_buf_sz)
			bufsize[i] = Mixer.limits[i].max_buf_sz;

		assert((bufsize[i] % 8) == 0);
		totsize += bufsize[i];
	}

	// Do one large allocations for all sample buffers
	assert(Mixer.ch_buf_mem == NULL);
	Mixer.ch_buf_mem = malloc(totsize);
	uint8_t *cur = Mixer.ch_buf_mem;

	// Initialize the sample buffers
	for (int i=0;i<Mixer.num_channels;i++) {
		samplebuffer_init(&Mixer.ch_buf[i], cur, bufsize[i]);
		cur += bufsize[i];
	}

	assert(cur == Mixer.ch_buf_mem+totsize);
}

int mixer_sample_rate(void) {
	assert(mixer_initialized());
	return Mixer.sample_rate;
}

void mixer_set_vol(float vol) {
	Mixer.vol = vol;
}

void mixer_close(void) {
	assert(mixer_initialized());

	set_AI_interrupt(0);
	unregister_AI_handler(mixer_interrupt);

	if (Mixer.ch_buf_mem) {
		free(Mixer.ch_buf_mem);
		Mixer.ch_buf_mem = NULL;
	}

	if (Mixer.buffer != NULL) {
		free(Mixer.buffer);
		Mixer.buffer = NULL;
	}

	Mixer.num_channels = 0;
}

void mixer_ch_set_freq(int ch, float frequency) {
	mixer_channel_t *c = &Mixer.channels[ch];
	c->step = MIXER_FX32(frequency / (float)Mixer.sample_rate) << (c->flags & CH_FLAGS_BPS_SHIFT);
}

void mixer_ch_set_vol(int ch, float lvol, float rvol) {
	Mixer.lvol[ch] = MIXER_FX15(lvol);
	Mixer.rvol[ch] = MIXER_FX15(rvol);
}

void mixer_ch_set_vol_pan(int ch, float vol, float pan) {
	mixer_ch_set_vol(ch, vol * (1.f - pan), vol * pan);
}

void mixer_ch_set_vol_dolby(int ch, float fl, float fr,
	float c, float sl, float sr) {

	#define SQRT_05   0.7071067811865476f
	#define SQRT_075  0.8660254037844386f
	#define SQRT_025  0.5f

	#define KF        1.0f
	#define KC        SQRT_05
	#define KA        SQRT_075
	#define KB        SQRT_025

	#define KTOT      (KF+KC+KA+KB)
	#define KFn       (KF/KTOT)
	#define KCn       (KC/KTOT)
	#define KAn       (KA/KTOT)
	#define KBn       (KB/KTOT)

	mixer_ch_set_vol(ch,
		fl*KFn + c*KCn - sl*KBn - sr*KBn,
		fr*KFn + c*KCn + sl*KBn + sr*KBn
	);
}

// Given a position within a looping waveform, calculate its wrapped position
// in the range [0, len], according to loop definition.
// NOTE: this function should only be called on looping waveforms.
static int waveform_wrap_wpos(int wpos, int len, int loop_len) {
	assert(loop_len != 0);
	assert(wpos >= len);
	return ((wpos - len) % loop_len) + (len - loop_len);
}

// A wrapper for a waveform's read function that handles loops.
// Sample buffers are not aware of loops. The way the mixer handles
// loops is by unrolling them in the sample buffer: that is, the sample
// buffer is called with an unlimited growing wpos, and the
// WaveformRead callback is expected to unroll the loop as wpos
// grows. To alleviate all waveforms implementations to handle loop
// unrolling, this simple wrapper performs the wpos wrapping calculations
// and convert it in a sequence of calls to read callbacks using only positions
// in the range [0, len].
static void waveform_read(void *ctx, samplebuffer_t *sbuf, int wpos, int wlen, bool seeking) {
	waveform_t *wave = (waveform_t*)ctx;

	if (!wave->loop_len) {
		// No loop defined: just call the waveform's read function.
		wave->read(wave->ctx, sbuf, wpos, wlen, seeking);
	} else {
		// Calculate wrapped position
		if (wpos >= wave->len)
			wpos = waveform_wrap_wpos(wpos, wave->len, wave->loop_len);

		// The read might cross the end point of the waveform
		// and continue at the loop point. We would need to handle
		// this case by performing two reads with a seek inbetween.

		// Split the length into two segments: before loop and loop.
		int len1 = wlen;
		if (wpos + wlen > wave->len)
			len1 = wave->len - wpos;
		int len2 = wlen-len1;

		// Logic check: the second segment (loop) shouldn't be longer
		// than the loop length plus the loop overread. Otherwise, it means
		// that we've been requested a single read that spans more than two
		// full loops, but that's impossible! In fact, a single request must fit
		// a sample buffer, and if a whole loop fits the sample buffer,
		// we wouldn't get here: the mixer handles fully-cachable loops
		// without unrolling them (see mixer_poll).
		assertf(len2 <= wave->loop_len + (MIXER_LOOP_OVERREAD >> SAMPLES_BPS_SHIFT(sbuf)),
			"waveform %s: logic error: double loop in single read\n"
			"wpos:%x, wlen:%x, len:%x loop_len:%x",
			wave->name, wpos, wlen, wave->len, wave->loop_len);

		// Perform the first read
		wave->read(wave->ctx, sbuf, wpos, len1, seeking);

		// See if we need to perform a second read for the loop. Because of
		// overread, we need to read the loop as many times as necessary
		// (though technically, once would be sufficient without overread).
		while (len2 > 0) {
			int loop_start = wave->len - wave->loop_len;
			int ns = MIN(len2, wave->loop_len);
			wave->read(wave->ctx, sbuf, loop_start, ns, true);
			len2 -= ns;
		}
	}
}

void mixer_ch_play(int ch, waveform_t *wave) {
	samplebuffer_t *sbuf = &Mixer.ch_buf[ch];
	mixer_channel_t *c = &Mixer.channels[ch];

	if (!Mixer.ch_buf_mem) {
		// If we have not yet allocated the memory for the sample buffers,
		// this is a good moment to do so, as we might need the configure
		// the samplebuffer in a moment.
		mixer_init_samplebuffers();
	}

	// Configure the waveform on this channel, if we have not
	// already. This optimization is useful in case the caller
	// wants to play the same waveform on the same channel multiple
	// times, and the waveform has been already decoded and cached
	// in the sample buffer.
	if (wave != sbuf->wv_ctx) {
		samplebuffer_flush(sbuf);

		// Configure the sample buffer for this waveform
		assert(wave->nbits == 8 || wave->nbits == 16);
		samplebuffer_set_bps(sbuf, wave->nbits);
		samplebuffer_set_decoder(sbuf, wave->read ? waveform_read : NULL, wave);

		// Configure the mixer channel structured used by the RSP ucode
		int bps = SAMPLES_BPS_SHIFT(sbuf);
		c->flags = bps;
		c->len = MIXER_FX32(wave->len) << bps;
		c->loop_len = MIXER_FX32(wave->loop_len) << bps;
		mixer_ch_set_freq(ch, wave->frequency);
		tracef("mixer_ch_play: ch=%d len=%lx loop_len=%lx\n", ch, c->len >> MIXER_FX32_FRAC, c->loop_len >> MIXER_FX32_FRAC);
	}

	// Restart from the beginning of the waveform
	c->ptr = SAMPLES_PTR(sbuf);
	c->pos = 0;
}

void mixer_ch_set_pos(int ch, float pos) {
	mixer_channel_t *c = &Mixer.channels[ch];
	c->pos = MIXER_FX32(pos) << (c->flags & CH_FLAGS_BPS_SHIFT);
}

float mixer_ch_get_pos(int ch) {
	mixer_channel_t *c = &Mixer.channels[ch];
	uint32_t pos = c->pos >> (c->flags & CH_FLAGS_BPS_SHIFT);
	return (float)pos / (float)(1<<MIXER_FX32_FRAC);
}

void mixer_ch_stop(int ch) {
	Mixer.channels[ch].ptr = 0;

	// Restart caching if played again. We need this guarantee
	// because after calling stop(), the caller must be able
	// to free waveform, and thus this pointer might become invalid.
	Mixer.ch_buf[ch].wv_ctx = NULL;
}

bool mixer_ch_playing(int ch) {
	return Mixer.channels[ch].ptr != 0;
}

void mixer_ch_set_limits(int ch, int max_bits, float max_frequency, int max_buf_sz) {
	assert(max_bits == 0 || max_bits == 8 || max_bits == 16);
	assert(max_frequency >= 0);
	assert(max_buf_sz >= 0 && max_buf_sz % 8 == 0);

	Mixer.limits[ch] = (channel_limit_t){
		.max_bits = max_bits,
		.max_frequency = max_frequency,
		.max_buf_sz = max_buf_sz,
	};

	// Changing the limits will invalidate the whole sample buffer
	// memory area. Invalidate all sample buffers.
	if (Mixer.ch_buf_mem) {
		for (int i=0;i<Mixer.num_channels;i++)
			samplebuffer_close(&Mixer.ch_buf[i]);
		free(Mixer.ch_buf_mem);
		Mixer.ch_buf_mem = NULL;
	}
}

void mixer_exec(mixer_sample_t *out, int num_samples) {
	if (!Mixer.ch_buf_mem) {
		// If we have not yet allocated the memory for the sample buffers,
		// this is a good moment to do so.
		mixer_init_samplebuffers();
	}

	tracef("mixer_exec: 0x%x samples\n", num_samples);

    uint32_t fake_loop = 0;

	for (int i=0; i<Mixer.num_channels; i++) {
		samplebuffer_t *sbuf = &Mixer.ch_buf[i];
		mixer_channel_t *ch = &Mixer.channels[i];
		int bps = ch->flags & CH_FLAGS_BPS_SHIFT;
		int bps_fx32 = bps + MIXER_FX32_FRAC;

		if (ch->ptr) {
			int len = ch->len >> bps_fx32;
			int loop_len = ch->loop_len >> bps_fx32;
			int wpos = ch->pos >> bps_fx32;
			int wlast = (ch->pos + ch->step*(num_samples-1)) >> bps_fx32;
			int wlen = wlast-wpos+1;
			assertf(wlen >= 0, "channel %d: wpos overflow", i);
			tracef("ch:%d wpos:%x wlen:%x len:%x loop_len:%x sbuf_size:%x\n", i, wpos, wlen, len, loop_len, sbuf->size);

			if (!loop_len) {
				// If we reached the end of the waveform, stop the channel
				// by NULL-ing the buffer pointer.
				if (wpos >= len) {
					ch->ptr = 0;
					continue;
				}
				// When there's no loop, do not ask for more samples then
				// actually present in the waveform.
				if (wpos+wlen > len)
					wlen = len-wpos;
				assert(wlen >= 0);
			} else if (loop_len < sbuf->size) {
				// If the whole loop fits the sample buffer, we just need to
				// make sure that it is aligned at the start of the buffer, so
				// that it can be fully cached.
				// To do so, we discard everything that comes before the loop 
				// (once we enter the loop).
				int loop_pos = len - loop_len;
				if (wpos >= loop_pos) {
					tracef("ch:%d discard to align loop wpos:%x loop_pos:%x\n", i, wpos, loop_pos);
					samplebuffer_discard(sbuf, loop_pos);
				}

				// Do not ask more samples than the end of waveform. When we
				// get there, the loop has been already fully cached. The RSP
				// will correctly follow the loop.
				while (wpos >= len)
					wpos -= loop_len;
				if (wpos+wlen > len)
					wlen = len-wpos;

				// FIXME: due to a limit in the RSP ucode, we need to overread
				// more data past the loop end.
				wlen += MIXER_LOOP_OVERREAD >> bps;
				assertf(wlen >= 0, "ch:%d wlen=%x wpos=%x len=%x\n", i, wlen, wpos, len);
			} else {
				// The loop is larger than the sample buffer. We cannot fully
				// cache it, so we will have to unroll it in the sample buffer.
				// This happens by default without doing anything: wpos will
				// increase, and the actual unrolling logic will be performed
				// by waveform_read() (see above).

				// To avoid having wpos growing indefinitely (and overflowing),
				// let's force a manual wrapping of the coordinates. Check if
				// this is a good moment to do it.
				if (sbuf->wpos > len && wpos > len) {
					tracef("mixer_poll: wrapping sample buffer loop: sbuf->wpos:%x len:%x\n", sbuf->wpos, len);
					samplebuffer_discard(sbuf, wpos);
					sbuf->wpos = waveform_wrap_wpos(sbuf->wpos, len, loop_len);
					int wpos2 = waveform_wrap_wpos(wpos, len, loop_len);
					ch->pos -= (wpos-wpos2) << bps_fx32;
					wpos = wpos2;
				}

				// We will also lie to the RSP ucode telling it that there is
				// no loop in this waveform, since the RSP will always see
				// the loop unrolled in the buffer, so it doesn't need to
				// do anything.
				fake_loop |= 1<<i;
			}

			void* ptr = samplebuffer_get(sbuf, wpos, &wlen);
			assert(ptr);
			ch->ptr = (uint8_t*)ptr - (wpos<<bps);
		}
	}

	rsp_wait();
	rsp_load(&rsp_mixer);

	volatile mixer_channel_t *rsp_wv = (volatile mixer_channel_t *)&SP_DMEM[36];
	for (int ch=0;ch<Mixer.num_channels;ch++) {
		rsp_wv[ch].pos = Mixer.channels[ch].pos;
		rsp_wv[ch].step = Mixer.channels[ch].step;
		rsp_wv[ch].len = fake_loop & (1<<ch) ? 0x7FFFFFFF : Mixer.channels[ch].len;
		rsp_wv[ch].loop_len = fake_loop & (1<<ch) ? 0 : Mixer.channels[ch].loop_len;
		rsp_wv[ch].ptr = Mixer.channels[ch].ptr;
		rsp_wv[ch].flags = Mixer.channels[ch].flags;
	}

	mixer_fx15_t lvol[MIXER_MAX_CHANNELS] __attribute__((aligned(8)));
	mixer_fx15_t rvol[MIXER_MAX_CHANNELS] __attribute__((aligned(8)));

	for (int ch=0;ch<MIXER_MAX_CHANNELS;ch++)  {
		// Configure the volume to 0 when the channel is keyed off. This
		// makes sure that we smooth volume correctly even for waveforms
		// where the sequencer creates an a attack ramp (which would nullify
		// the one-tap volume filter if the volume started from max).
		bool on = Mixer.channels[ch].ptr != NULL;
		lvol[ch] = on ? Mixer.lvol[ch] : 0;
		rvol[ch] = on ? Mixer.rvol[ch] : 0;
	}

	// Copy the volumes into DMEM. TODO: check if should change this loop into
	// a DMA copy, or fold it into the above loop.
	uint32_t *lvol32 = (uint32_t*)lvol;
	uint32_t *rvol32 = (uint32_t*)rvol;
	for (int ch=0;ch<MIXER_MAX_CHANNELS/2;ch++)  {
		SP_DMEM[4+0*16+ch] = lvol32[ch];
		SP_DMEM[4+1*16+ch] = rvol32[ch];
	}

	SP_DMEM[0] = MIXER_FX16(Mixer.vol);
	SP_DMEM[1] = (num_samples << 16) | Mixer.num_channels;
	SP_DMEM[2] = (uint32_t)out;
	SP_DMEM[3] = (uint32_t)Mixer.ucode_state;

	rsp_run();

	for (int i=0;i<Mixer.num_channels;i++) {
		mixer_channel_t *ch = &Mixer.channels[i];
		ch->pos = rsp_wv[i].pos;
	}

	Mixer.ticks += num_samples;
}

static mixer_event_t* mixer_next_event(void) {
	mixer_event_t *e = NULL;
	for (int i=0;i<Mixer.num_events;i++) {
		if (!e || Mixer.events[i].ticks < e->ticks)
			e = &Mixer.events[i];
	}
	return e;
}

void mixer_add_event(int64_t delay, MixerEvent cb, void *ctx) {
	Mixer.events[Mixer.num_events++] = (mixer_event_t){
		.cb = cb,
		.ctx = ctx,
		.ticks = Mixer.ticks + delay
	};
}

void mixer_remove_event(MixerEvent cb, void *ctx) {
	for (int i=0;i<Mixer.num_events;i++) {
		if (Mixer.events[i].cb == cb) {
			memmove(&Mixer.events[i], &Mixer.events[i+1], sizeof(mixer_event_t) * (Mixer.num_events-i-1));
			Mixer.num_events--;
			return;
		}
	}
	assertf("mixer_remove_event: specified event does not exist\ncb:%p ctx:%p", (void*)cb, ctx);
}

static void mixer_exec_with_events(mixer_sample_t *out, int num_samples) {
    while (num_samples > 0) {
    	mixer_event_t *e = mixer_next_event();

    	int ns = MIN(num_samples, e ? e->ticks - Mixer.ticks : num_samples);
    	if (ns > 0) {
			mixer_exec(out, ns);
			out += ns;
			num_samples -= ns;
    	}
		if (e && Mixer.ticks == e->ticks) {
			int64_t repeat = e->cb(e->ctx);
			if (repeat)
				e->ticks += repeat;
			else
				mixer_remove_event(e->cb, e->ctx);
		}
	}

}

void mixer_poll(int num_samples) {
    // We can't be asked to produce more than the buffer size.
    assert(num_samples < Mixer.buf_size);

    // Since the AI can only play an even number of samples,
    // it's not possible to call this function with an odd number,
    // otherwise buffering might become complicated / impossible.
    assert(num_samples % 2 == 0);

    mixer_sample_t *out;
	volatile int *out_buf_w;

    disable_interrupts();
    if (Mixer.buf_w2 == 0 && Mixer.buf_w + num_samples <= Mixer.buf_size) {
    	// debugf("POLL1: buf_r=%x buf_w=%x buf_w2=%x\n", Mixer.buf_r, Mixer.buf_w, Mixer.buf_w2);
	    out = Mixer.buffer + Mixer.buf_w;
	    out_buf_w = &Mixer.buf_w;
    } else {
    	// debugf("POLL2: buf_r=%x buf_w=%x buf_w2=%x\n", Mixer.buf_r, Mixer.buf_w, Mixer.buf_w2);
    	out = Mixer.buffer + Mixer.buf_w2;
	    out_buf_w = &Mixer.buf_w2;
    	assert(Mixer.buf_r >= Mixer.buf_w2+num_samples);
    }
    enable_interrupts();
	assert(((uint32_t)out & 3) == 0);

	mixer_exec_with_events(out, num_samples);

    // FIXME: this should not be required
	while ((AI_regs->status & AI_STATUS_FULL)) {}

	disable_interrupts();
    *out_buf_w += num_samples;
	mixer_interrupt();
	enable_interrupts();
}

/***********************************************************************
 * Mixer videosync API
 ***********************************************************************/

static struct {
	float fps;
	float samples_per_frame;
	float counter;
	unsigned int current_frame_samples;
} MixerVS;

static inline int mixervs_initialized(void) { return MixerVS.fps != 0; }

void mixer_videosync_init(float fps) {
	assert(mixer_initialized());
	memset(&MixerVS, 0, sizeof(MixerVS));
	MixerVS.fps = fps;
	MixerVS.samples_per_frame = (float)Mixer.sample_rate / fps;
	mixer_videosync_next_frame();
}

unsigned int mixer_videosync_suggested_buffer_size(void) {
	assert(mixervs_initialized());

	// Size of a buffer requested by the mixer (for each audio interrupt).
	// This is chosen internally by libdragon and cannot be adjusted externally.
	unsigned int irq_buf_sz = audio_get_buffer_length();

	// Number of times the audio interrupt will trigger for each videoframe.
	// We need to make sure to round correctly here: the number of interrupts is
	// first truncated to the lower integer, and then incremented by 1 because
	// there can always be a phase misalignment.
	// For instance, if irq_buf_sz is 1000 and samples_per_frame is 5000, 
	// there are in theory 5 irqs per frame, but in practice we need to account
	// for 6 irqs as they will never be fully aligned.
	unsigned int irqs_per_frame = floorf(MixerVS.samples_per_frame / (float)irq_buf_sz) + 1;

	// Return the size of the buffer, rounded up to an even number of samples
	// so that the samples buffers will always be multiple of 8 bytes in size.
	// This make sure that DMA can be used to manipulate samples buffer.
	return ROUND_UP(irqs_per_frame * irq_buf_sz, 2);
}

unsigned int mixer_videosync_current_frame_samples(void) {
	assert(mixervs_initialized());
	return MixerVS.current_frame_samples;
}

void mixer_videosync_next_frame(void) {
	assert(mixervs_initialized());
	MixerVS.counter += MixerVS.samples_per_frame;
	if (MixerVS.counter < 0) {
		MixerVS.current_frame_samples = 0;
		return;
	}

	// Get the correct number of samples that should be produced for this
	// frame.
	MixerVS.current_frame_samples = ceilf(MixerVS.counter);

	// Round up to 2 samples (8 bytes) to allow for DMA usage.
	MixerVS.current_frame_samples = ROUND_UP(MixerVS.current_frame_samples, 2);

	// Decrement the counter by the number of samples that have been
	// accounted for this frame, so that any excess samples is correctly
	// accounted and will reduce the number of samples that will be needed
	// for next frame.
	MixerVS.counter -= MixerVS.current_frame_samples;
}

/***********************************************************************
 * Sample buffer API
 ***********************************************************************/

void samplebuffer_init(samplebuffer_t *buf, uint8_t* mem, int nbytes) {
	memset(buf, 0, sizeof(samplebuffer_t));
	buf->ptr_and_flags = (uint32_t)mem;
	assert((buf->ptr_and_flags & 7) == 0);
	buf->size = nbytes;
	// Make sure there is no CPU cache content in the buffer
	data_cache_hit_writeback_invalidate((void*)buf->ptr_and_flags, nbytes);
}

void samplebuffer_set_bps(samplebuffer_t *buf, int bits_per_sample) {
	assert(bits_per_sample == 8 || bits_per_sample == 16);
	assertf(buf->widx == 0 && buf->ridx == 0 && buf->wpos == 0,
		"samplebuffer_set_bps can only be called on an empty samplebuffer");

	int nbytes = buf->size << SAMPLES_BPS_SHIFT(buf);

	int bps = bits_per_sample == 8 ? 0 : 1;
	buf->ptr_and_flags = SAMPLES_PTR_MAKE(SAMPLES_PTR(buf), bps);
	buf->size = nbytes >> bps;
}

void samplebuffer_set_decoder(samplebuffer_t *buf, WaveformRead read, void *ctx) {
	buf->wv_read = read;
	buf->wv_ctx = ctx;
}

void samplebuffer_close(samplebuffer_t *buf) {
	buf->ptr_and_flags = 0;
}

void* samplebuffer_get(samplebuffer_t *buf, int wpos, int *wlen) {
	// ROUNDUP8_BPS rounds up the specified number of samples
	// (given the bps shift) so that they span an exact multiple
	// of 8 bytes. This will be applied to the number of samples
	// requested to wv_read(), to make sure that we always
	// keep the sample buffer filled with a multiple of 8 bytes.
	#define ROUNDUP8_BPS(nsamples, bps) \
		(((nsamples)+((8>>(bps))-1)) >> (3-(bps)) << (3-(bps)))

	int bps = SAMPLES_BPS_SHIFT(buf);

	tracef("samplebuffer_get: wpos=%x wlen=%x\n", wpos, *wlen);

	if (buf->widx == 0 || wpos < buf->wpos || wpos > buf->wpos+buf->widx) {
		// If the requested position is totally outside
		// the existing range (and not even consecutive),
		// we assume the mixer had to seek. So flush the
		// buffer and decode from scratch with seeking.
		samplebuffer_flush(buf);
		buf->wpos = wpos;
		buf->wv_read(buf->wv_ctx, buf, buf->wpos, ROUNDUP8_BPS(*wlen, bps), true);
	} else {
		// Record first sample that we still need to keep in the sample
		// buffer. This is important to do now because decoder_read might
		// push more samples than required into the buffer and force
		// to compact the buffer. We thus need to know which samples
		// are still required.
		buf->ridx = wpos - buf->wpos;

		// Part of the requested samples are already in the sample buffer.
		// Check how many we can reuse. For instance, if there's a waveform
		// loop, the whole loop might already be in the sample buffer, so
		// no further decoding is necessary.
		int reuse = buf->wpos + buf->widx - wpos;

		// If the existing samples are not enough, read the missing
		// through the callback.
		if (reuse < *wlen)
			buf->wv_read(buf->wv_ctx, buf, wpos+reuse, ROUNDUP8_BPS(*wlen-reuse, bps), false);
	}

	assertf(wpos >= buf->wpos && wpos < buf->wpos+buf->widx, 
		"samplebuffer_get: logic error\n"
		"wpos:%x buf->wpos:%x buf->widx:%x", wpos, buf->wpos, buf->widx);

	int idx = wpos - buf->wpos;

	// If the sample buffer contains less samples than requested,
	// report that by updating *wlen. This will cause cracks in the
	// audio as silence will be inserted by the mixer.
	int len = buf->widx - idx;
	if (len < *wlen)
		*wlen = len;

	return SAMPLES_PTR(buf) + (idx << SAMPLES_BPS_SHIFT(buf));
}

void* samplebuffer_append(samplebuffer_t *buf, int wlen) {
	// If the requested number of samples doesn't fit the buffer, we
	// need to make space for it by discarding older samples.
	if (buf->widx + wlen > buf->size) {
		// Make space in the buffer by discarding everything up to the
		// ridx index, which is the first sample that we still need for playback.
		assertf(buf->widx >= buf->ridx,
			"samplebuffer_append: invalid consistency check\n"
			"widx:%x ridx:%x\n", buf->widx, buf->ridx);

		// Rollback ridx until it hit a 8-byte aligned position.
		// This preserves the guarantee that samplebuffer_append
		// will always return a 8-byte aligned pointer, which is
		// good for DMA purposes.
		int ridx = buf->ridx;
		while ((ridx << SAMPLES_BPS_SHIFT(buf)) & 7)
			ridx--;
		samplebuffer_discard(buf, buf->wpos+ridx);
	}

	// If there is still not space in the buffer, it means that the
	// buffer is too small for this append call. This is a logic error,
	// so better assert right away.
	// TODO: in principle, we could bubble this error up to the callers,
	// let them fill less samples than requested, and obtain some cracks
	// in the audio. Is it worth it?
	assertf(buf->widx + wlen <= buf->size,
		"samplebuffer_append: buffer too small\n"
		"ridx:%x widx:%x wlen:%x size:%x", buf->ridx, buf->widx, wlen, buf->size);

	void *data = SAMPLES_PTR(buf) + (buf->widx << SAMPLES_BPS_SHIFT(buf));
	buf->widx += wlen;
	return data;
}

void samplebuffer_discard(samplebuffer_t *buf, int wpos) {
	int idx = wpos - buf->wpos;
	if (idx <= 0)
		return;
	if (idx > buf->widx)
		idx = buf->widx;

	tracef("discard: wpos=%x idx:%x buf->wpos=%x buf->widx=%x\n", wpos, idx, buf->wpos, buf->widx);
	int kept_bytes = (buf->widx - idx) << SAMPLES_BPS_SHIFT(buf);
	if (kept_bytes > 0) {		
		tracef("samplebuffer_discard: compacting buffer, moving 0x%x bytes\n", kept_bytes);

		// FIXME: this violates the zero-copy principle as we do a memmove here.
		// The problem is that the RSP ucode doesn't fully support a circular
		// buffer of samples (and also our samplebuffer_t isn't structured for
		// this). Luckily, this is a rare chance and in most cases just a few
		// samples are moved (in the normal playback case, it should be just 1,
		// as in general a sample could be used more than once for resampling).
		uint8_t *src = SAMPLES_PTR(buf) + (idx << SAMPLES_BPS_SHIFT(buf));
		uint8_t *dst = SAMPLES_PTR(buf);

		// Optimized copy of samples. We work on uncached memory directly
		// so that we don't need to flush, and use only 64-bits ops. We round up
		// to a multiple of 8 the amount of bytes, as it doesn't matter if we
		// copy more, as long as we're fast.
		// This has been benchmarked to be faster than memmove() + cache flush.
		typedef uint64_t u_uint64_t __attribute__((aligned(1)));
		kept_bytes = ROUND_UP(kept_bytes, 8);
		u_uint64_t *src64 = (u_uint64_t*)UncachedAddr(src);
		u_uint64_t *dst64 = (u_uint64_t*)UncachedAddr(dst);
		for (int i=0;i<kept_bytes/8;i++)
			*dst64++ = *src64++;
	}

	buf->wpos += idx;
	buf->widx -= idx;
	buf->ridx -= idx;
	if (buf->ridx < 0)
		buf->ridx = 0;
}

void samplebuffer_flush(samplebuffer_t *buf) {
	buf->wpos = buf->widx = buf->ridx = 0;
}
