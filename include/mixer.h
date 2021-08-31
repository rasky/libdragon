// Mixer - flexible, composable, fast, RSP-based audio mixer of libdragon
//
// This module offers a flexible API to mix and play up to 32 independent audio
// streams called "waveforms". It also supports resampling: each waveform can
// play at a different playback frequency, which in turn can be different from
// the final output frequency. The resampling and mixing is performed by a very
// efficient RSP microcode (see rsp_mixer.S).
//
// The mixer exposes 32 channels that can be used to play different audio sources.
// An audio source is called a "waveform", and is represented by the type
// waveform_t. To be able to produce audio that can be mixed (eg: decompress
// and playback a MP3 file), the decoder/player code must implement a waveform_t.
//
// waveform_t can be thought of as a "sample generator". The mixer does not force
// the whole audio stream to be decompressed in memory before playing, but rather
// work using callbacks: whenever new samples are required, a specific callback
// (defined in the waveform_t instance) is called, and the callback is expected
// to decompress and store a chunk of samples into a buffer provided by the mixer.
// This allows full decoupling between the mixer and the different audio players.
// Audio players are free to generate audio samples as they wish (eg: via RSP),
// as long as they honor the waveform_t protocol. See waveform_t for more
// information.
//
// One of the main design goals for the mixer is to provide an efficient way to
// compose different audio sources. To achieve full flexibility without impacting
// speed, the mixer tries to honor the "CPU zero copy" design principle: samples are
// (almost) never touched or moved around with CPU. The whole API has been
// carefully designed so that audio players can produce samples directly into
// a mixer-provided buffer, so that the mixer can then resample and mix them on
// the RSP and store the final output stream directly in the final buffer which
// is sent via DMA to the N64 AI hardware DAC. In general, the N64 CPU is very
// bandwidth-limited on RDRAM accesses, so "touching" 44100 samples per second
// with the PCU would already consume too many resources. Audio players are
// expected to use RSP to produce the audio samples as well, so that the full
// audio pipeline can work totally off CPU.

#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of channels supported by the mixer
#define MIXER_MAX_CHANNELS      32

// Tagged pointer to an array of samples. It contains both the void*
// sample pointer, and byte-per-sample information (encoded as shift value).
typedef uint32_t sample_ptr_t;

// SAMPLES_BPS_SHIFT extracts the byte-per-sample information from a sample_ptr_t.
// Byte-per-sample is encoded as shift value, so the actual number of bits is
// 1 << BPS. Valid shift values are 0, 1, 2 (which corresponds to 1, 2 or 4
// bytes per sample). 
#define SAMPLES_BPS_SHIFT(buf)      ((buf)->ptr_and_flags & 3)

// SAMPLES_PTR extract the raw void* to the sample array. The size of array
// is not encoded in the tagged pointer. Notice that it is implemented with a
// XOR because on MIPS it's faster than using a reverse mask.
#define SAMPLES_PTR(buf)            (void*)((buf)->ptr_and_flags ^ SAMPLES_BPS_SHIFT(buf))

// SAMPLES_PTR_MAKE create a tagged pointer, given a pointer to an array of
// samples and a byte-per-sample value (encoded as shift value).
#define SAMPLES_PTR_MAKE(ptr, bps)  ((sample_ptr_t)(ptr) | (bps))

// Forward declarations
typedef struct waveform_s waveform_t;
typedef struct samplebuffer_s samplebuffer_t;

/*********************************************************************
 *
 * MIXER API
 *
 *********************************************************************/

// Initialize the mixer with the specified number of channels and
// output sample rate.
void mixer_init(int num_channels, int sample_rate);

// Return the mixer sample rate.
// The returned value might slightly differ from the value
// specified in mixer_init() because it reflects the actual
// rate that is obtained from the audio DAC.
int mixer_sample_rate(void);

// Set master volume. This is a global attenuation
// factor (range [0..1]) that will be applied to all
// channels and simplify implementing a global volume
// control.
void mixer_set_vol(float vol);

// Configure the left and right channel volumes for the specified channel.
//
// The volume is an attenuation (no amplification is performed).
// Valid volume range in [0..1], where 0 is silence and 1 is original
// channel sample volume (no attenuation performed).
//
// Notice that it's perfectly valid to set left/right volumes on mono channels,
// as it allows to balance a mono sample between the two final output channels.
void mixer_ch_set_vol(int ch, float lvol, float rvol);

// Configure the left and right channel volumes for the specified channel,
// Using a central volume value and a panning value to specify left/right
// balance.
// Valid volume range in [0..1], where 0 is silence and 1 is maximum
// volume (no attenuation).
// Valid panning range is [0..1] where 0 is 100% left, and 1 is 100% right.
// Notice that panning 0.5 balance the sound but causes an attenuation of 50%.
void mixer_ch_set_vol_pan(int ch, float vol, float pan);

// Configure the volumes of the specified channel according to the Dolby Pro
// Logic II matrix encoding. This allows to encode samples with a virtual surround
// system, that can be decoded with a Dolby 5.1 compatible equipment.
//
// The function accepts the volumes configured for the 5 channels: front left,
// front right, center, surround left, surround right. These values can be
// calculated from a 3D scene
void mixer_ch_set_vol_dolby(int ch, float fl, float fr,
    float c, float sl, float sr);

// Start playing the specified waveform on the specified channel. This
// immediately begins playing the waveform, interrupting any other waveform
// that might have been reproduced on this channel.
void mixer_ch_play(int ch, waveform_t *wave);

// Change the frequency for the specified channel. By default, the frequency
// is the one requested by the waveform associated to the channel, but this
// function allows to override.
//
// This function must be called after mixer_ch_play, as otherwise the
// frequency is reset to the default of the waveform.
void mixer_ch_set_freq(int ch, float frequency);

// Change the current playback position in the channel. This can be useful
// to seek to a specific point of the waveform. The position must be specified
// in number of samples (not bytes). Fractional values account for accurate
// resampling position.
//
// This function must be called after mixer_ch_play, as otherwise the
// position is reset to the beginning of the waveform.
void mixer_ch_set_pos(int ch, float pos);

// Read the current playback position of the waveform in the channel, as
// number of samples. Fractional values account for accurate resampling
// position. 
float mixer_ch_get_pos(int ch);

// Stop playing samples on the specified channel.
void mixer_ch_stop(int ch);

// Return true if the channel is currently playing samples.
bool mixer_ch_playing(int ch);

// Configure the limits of a channel with respect to sample bit size, and
// frequency.
//
// This is an advanced function that should be used with caution, only in
// situations in which it is paramount to control the memory usage of the mixer.
//
// By default, each channel in the mixer is capable of doing 16-bit playback
// with a frequency up to the mixer output sample rate (eg: 44100hz). This means
// that the mixer will allocate sample buffers required for this kind of
// capability.
//
// If it is known that certain channels will use only 8-bit waveforms and/or
// a lower frequency, it is possible to call this function to inform the mixer
// of these limits. This will cause the mixer to reallocate the samplebuffers
// lowering its memory usage (note: multiple calls to this function for different
// channels will of course be batched to cause only one reallocation).
//
// Note also that this function can be used to increase the maximum frequency
// over the mixer sample rate, in case this is required. This works correctly
// but since it causes downsampling, it is generally a waste of memory bandwidth
// and processing power.
//
// "max_buf_sz" can be used to limit the maximum buffer size that will be
// allocated for this channel (in bytes). This is a hard cap, applied on top
// of the optimal buffer size that will be calculated by "max_bits" and
// "max_frequency", and can be used in situations where there are very strong
// memory constraints that must be respected. Use 0 if you don't want to impose
// a limit.
void mixer_ch_set_limits(int ch, int max_bits, float max_frequency, int max_buf_sz);


// Run the mixer to produce output samples. This function will fetch
// required samples from all the channels and mix them together according
// to each channel's settings. The output will be fed directly into the
// N64 audio system (AI).
//
// nsamples is the number of samples that should be produced. Depending on
// how the audio should be synchronized with the video, there are two 
// possible approaches:
//
//   * If audio and video must be perfectly synchronized (eg: full motion
//     videos, or animated cutscene with music cues), in case the video slows
//     down, it is important that the audio will keep the sync by pausing
//     as well (this will cause some cracks in the audio, but it's the only
//     way to keep the perfect sync). In this case, use the videosync API
//     to calculate the exact number of audio samples to produce for each
//     video frame, and pass it to mixer_poll() by calling it once per frame.
//   * If audio is just a background music with no specific syncing, it is
//     preferable for audio to continue interrupted even when video slows down,
//     as human perception is much more sensible to audio cracks than to 
//     a slight change in video frame rate. In this case, you can pass -1
//     to mixer_poll() which means "produce as many samples as possible,
//     filling the audio buffers". See audio_init() to declare the number
//     of buffers which has an effect on how much buffering you want to 
//     perform (more buffers mean that you're more resilient to video frame
//     slowdowns, but also that more latency will be added to sound effects).
//
// mixer_poll performs mixing using RSP. If RSP is busy, mixer_poll will spinwait
// until the RSP is free, to perform audio processing.
//
// Since the N64 AI can only be fed with an even number of samples, mixer_poll
// does not accept odd numbers.
// 
void mixer_poll(int nsamples);

// A MixerEvent is a callback that is invoked during mixer_poll at a specified
// moment in time (that is, after a specified number of samples have been
// processed). It is useful to implement sequencers that need to update their
// status after a certain number of samples, or effects like volume envelopes
// that must update the channel volumes at regular intervals.
//
// "ctx" is an opaque pointer that provides a context to the callback.
//
// If the callback has finished its task, it can return 0 to deregister itself
// from the mixer. Otherwise, it can return a positive number of samples to
// wait before calling it again.
typedef int (*MixerEvent)(void* ctx);

// Register a new event into the mixer. "delay" is the number of samples to
// wait before calling the event callback. "cb" is the event callback. "ctx"
// is an opaque pointer that will be passed to the callback when invoked.
void mixer_add_event(int64_t delay, MixerEvent cb, void *ctx);

// Deregister an event from the mixer. "cb" is the event callback, and "ctx"
// is the opaque context pointer. Notice that an event can also deregister
// itself by returning 0 when called.
void mixer_remove_event(MixerEvent cb, void *ctx);


/*********************************************************************
 *
 * WAVEFORMS
 *
 *********************************************************************/

// WaveformRead is a callback function that will be invoked by the mixer
// whenever a new unavailable portion of the waveform is requested.
//
// "wpos" indicates the absolute position in the waveform from which
// starts reading, and "wlen" indicates how many samples to read.
//
// The read function should push into the provided sample buffer
// at least "wlen" samples, using samplebuffer_append. Producing more samples
// than requested is perfectly fine, they will be stored in the sample buffer
// and remain available for later use. For instance, a compressed waveform
// (eg: VADPCM) might decompress samples in "blocks" of fixed size, and thus
// push full blocks into the sample buffer, to avoid decoding a block twice.
//
// On the contrary, producing less samples should only be done
// if the read function has absolutely no way to produce more. This should
// happen only when the stream is finished, and only when the waveform length
// was unknown to the mixer (otherwise, the mixer won't ask more samples than
// available in the first place).
//
// The argument "seeking" is a flag that indicates whether the read being
// requested requires seeking or not. If "seeking" is false (most of the times),
// it means that the current read is requesting samples which come immediately
// after the ones that were last read; in other words, the implementation might
// decide to ignore the "wpos" argument and simply continue decoding the audio
// from the place were it last stopped. If "seeking" is true, instead, it means
// that there was a jump in position that should be taken into account.
//
// Normally, a seeking is required in the following situations:
//
//   * At start (mixer_ch_play): the first read issued by the mixer will
//     be at position 0 with seeking == true (unless the waveform was
//     already partially cached in the sample buffer's channel).
//   * At user request (mixer_ch_set_pos).
//   * At loop: if a loop is specified in the waveform (loop_len != 0), the
//     mixer will seek when requires to execute the loop.
//
// Notice that producing more samples than requested in "wlen" might break
// the 8-byte buffer alignment guarantee that samplebuffer_append tries to
// provide. For instance, if the read function is called requesting 24 samples,
// but it produces 25 samples instead, the alignment in the buffer will be lost,
// and next call to samplebuffer_append will return an unaligned pointer.
//
// "ctx" is an opaque pointer that is provided as context to the function, and
// is specified in the waveform.
typedef void (*WaveformRead)(void *ctx, samplebuffer_t *sbuf, int wpos, int wlen, bool seeking);

// waveform_t represents a waveform that can be played back by the mixer.
// A waveform_t does not hold the actual samples because most real-world use
// cases do not keep all samples in memory, but rather load them and/or
// decompress them in real-time while the playback is happening.
// So waveform_t instead should be thought of as the generator of a
// waveform.
//
// To create a waveform, use one of players such as Sfx64Player. Players
// are in charge of generating the waveform by actually implementing an audio
// format like VADPCM or MPEG-2.
//
// Waveforms can produce samples as 8-bit or 16-bit. Samples must always be
// signed. Stereo waveforms are currently not supported. If an audio format
// encodes two channels for stereo playback, the player should expose two
// different waveforms that must be played in two different mixer channels.
typedef struct waveform_s {
    // Name of the waveform (for debugging purposes)
    const char *name;

    // Width of a sample of this waveform, in bits. Supported values are 8 or 16.
    // Notice that samples must always be signed.
    int nbits;

    // Desired playback frequency (in samples per second, aka Hz).
    float frequency;

    // Length of the waveform, in number of samples. If the length is not
    // known, this value can be initialized to a very large number (eg: 2**31-1)
    // It's still possible to play a waveform this way, but the read function
    // will have to generate silence when the actual waveform is finished.
    int len;

    // Length of the loop of the waveform (from the end). This value describes
    // how many samples of the tail of the waveform needs to be played in a loop.
    // For instance, if len==1000 and loop_len=500, the waveform will be played
    // once, and then its second half will be repeated in loop.
    int loop_len;

    // Read function of the waveform. This is the callback that will be invoked
    // by the mixer to generate the samples (technically, it is invoked by
    // the sample buffer associated to a specific channel of the mixer).
    // See WaveformRead for more information.
    WaveformRead read;

    // An opaque pointer which will be provided as context to the read/seek
    // functions.
    void *ctx;
} waveform_t;


/*********************************************************************
 *
 * SAMPLE BUFFER
 *
 *********************************************************************/

/**
 * samplebuffer_t is a circular buffer of samples. It is used by the mixer
 * to store and cache the samples required for playback on each channel.
 * The mixer creates a sample buffer for each initialized channel. The size
 * of the buffers is calculated for optimal playback, and might grow depending
 * on channel usage (what waveforms are played on each channel).
 *
 * The mixer follows a "pull" architecture. During mixer_poll, it will call
 * samplebuffer_get() to extract samples from the buffer. If the required
 * samples are not available, the sample buffer will callback the waveform
 * decoder to produce more samples, through the WaveformRead API. The
 * waveform read function will push samples into the buffer via samplebuffer_append,
 * so that they become available for the mixer. The decoder can be configured
 * with samplebuffer_set_decoder.
 *
 * The current implementation of samplebuffer does not achieve full zero copy,
 * because when the buffer is full, it is flushed and samples that need to 
 * be preserved (that is, already in the buffer but not yet played back) are
 * copied back at the beginning of the buffer with the CPU. This limitation
 * exists because the RSP ucode (rsp_audio.S) isn't currently able to "wrap around"
 * in the sample buffer. In future, this limitation could be lifted to achieve
 * full zero copy.
 *
 * The sample buffer tries to always stay 8-byte aligned to simplify operations
 * of decoders that might need to use DMA transfers (either PI DMA or RSP DMA).
 * To guarantee this property, SampleDecoder_Read must collaborate by decoding
 * the requested number of samples. If SampleDecoder_Read decodes a different
 * number of samples, the alignment might be lost.
 *
 * In general, the sample buffer assumes that the contained data is committed
 * to physical memory, not just CPU cache. It is responsibility of the client
 * to flush DMA cache (via data_cache_writeback) if samples are written
 * via CPU.
 */
typedef struct samplebuffer_s {
    /**
     * Tagged pointer to the actual buffer. Lower bits contain bit-per-shift.
     */
    sample_ptr_t ptr_and_flags;

    /** 
     * Size of the buffer (in samples).
     **/
    int size;

    /**
     * Absolute position in the waveform of the first sample
     * in the sample buffer (the sample at index 0). It keeps track of
     * which part of the waveform this sample buffer contains.
     */
    int wpos;

    /**
     * Write pointer in the sample buffer (expressed as index of samples).
     * Since sample buffers are always filled from index 0, it is also
     * the number of samples stored in the buffer.
     */
    int widx;

    /**
     * Read pointer in the sample buffer (expressed as index of samples).
     * It remembers which sample was last read. Assuming a forward
     * streaming, it is used by the sample buffer to discard unused samples
     * when not needed anymore.
     */
    int ridx;

    /*/
     * wv_read is invoked by samplebuffer_get whenever more samples are
     * requested by the mixer. See WaveformRead for more information.
     */
    WaveformRead wv_read;

    /**
     * wv_ctx is the opaque pointer to pass as context to decoder functions.
     */
    void *wv_ctx;
} samplebuffer_t;

/**
 * Initialize the sample buffer by binding it to the specified memory buffer.
 *
 * The sample buffer is guaranteed to be 8-bytes aligned, so the specified
 * memory buffer must follow this constraints. If the decoder respects 
 * the "wlen" argument passed to WaveformRead callback, the buffer returned
 * by samplbuffer_append will always be 8-byte aligned and thus suitabl
 * for DMA transfers. Notice that it's responsibility of the client to flush
 * the cache if the DMA is used.
 * 
 * @param[in]   buf     Sample buffer
 * @param[in]   mem     Memory buffer to use. Must be 8-byte aligned.
 * @param[in]   size    Size of the memory buffer, in bytes.
 */
void samplebuffer_init(samplebuffer_t *buf, uint8_t *mem, int size);

/**
 * @brief Configure the bit width of the samples stored in the buffer.
 * 
 * Valid values for "bps" are 1, 2, or 4: 1 can be used for 8-bit mono samples,
 * 2 for either 8-bit interleaved stereo or 16-bit mono, and 4 for 16-bit
 * interleaved stereo.
 * 
 * @param[in]   buf     Sample buffer
 * @param[in]   bps     Bytes per sample.
 */
void samplebuffer_set_bps(samplebuffer_t *buf, int bps);

/**
 * Connect a sample decoder (aka player) to this sample buffer. The decoder
 * will be use to produce samples whenever they are required by the mixer
 * as playback progresses.
 *
 * "read" is the main decoding function, that is invoked to produce a specified
 * number of samples. Normally, the function is invoked by samplebuffer_get,
 * whenever the mixer requests more samples. The read function can assume
 * forward playback, so it can just keep decoding samples in sequence when
 * invoked. See WaveformRead for more information.
 * 
 * @param[in]       buf     Sample buffer
 * @param[in]       read    Waveform reading function, that produces samples.
 * @param[in]       ctx     Opaque context that will be passed to the read
 *                          function.
 */
void samplebuffer_set_decoder(samplebuffer_t *buf, WaveformRead read, void *ctx);

/**
 * @brief Get a pointer to specific set of samples in the buffer (zero-copy).
 * 
 * "wpos" is the absolute waveform position of the first sample that the
 * caller needs access to. "wlen" is the number of requested samples.
 *
 * The function returns a pointer within the sample buffer where the samples
 * should be read, and optionally changes "wlen" with the maximum number of
 * samples that can be read. "wlen" is always less or equal to the requested value.
 *
 * If the samples are available in the buffer, they will be returned immediately.
 * Otherwise, if the samplebuffer has a sample decoder registered via
 * samplebuffer_set_decoder, the decoder "read" function is called once to
 * produce the samples.
 *
 * If "wlen" is changed with a value less than "wlen", it means that
 * not all samples were available in the buffer and it was not possible to
 * generate more, so the caller should not loop calling this function, but
 * rather use what was obtained and possibly pad with silence.
 *
 * @param[in]       buf     Sample buffer
 * @param[in]       wpos    Absolute waveform position of the first samples to
 *                          return.
 * @param[in,out]   wlen    Number of samples to return. After return, it is
 *                          modified with the actual number of samples that
 *                          have been returned.
 * @return                  Pointer to samples.
 */
void* samplebuffer_get(samplebuffer_t *buf, int wpos, int *wlen);

/**
 * @brief Append samples into the buffer (zero-copy). 
 *
 * "wlen" is the number of samples that the caller will append.
 *
 * The function returns a pointer within the sample buffer where the samples
 * should be written. Notice that since audio samples are normally processed
 * via DMA/RSP, it is responsibility of the caller to actually force a cache
 * writeback (with data_cache_writeback) in case the samples are written
 * using CPU. In other words, this function expects samples to be written to
 * physical memory, not just CPU cache.
 *
 * The function is meant only to "append" samples, as in add samples that are
 * consecutive within the waveform to the ones already stored in the sample
 * buffer. This is necessary because samplebuffer_t can only store a single
 * range of samples of the waveform; there is no way to hold two disjoint
 * ranges.
 *
 * For instance, if the sample buffer currently contains 50 samples
 * starting from position 100 in the waverform, the next call to
 * samplebuffer_append will append samples starting at 150.
 *
 * If required, samplebuffer_append will discard older samples to make space
 * for the new ones, through samplebuffer_discard. It will only discard samples
 * that come before the "wpos" specified in the last samplebuffer_get call, so
 * to make sure that nothing required for playback is discarded. If there is
 * not enough space in the buffer, it will assert.
 * 
 * @param[in]   buf     Sample buffer
 * @param[in]   wlen    Number of samples to append.
 * @return              Pointer to the area where new samples can be written.
 */
void* samplebuffer_append(samplebuffer_t *buf, int wlen);

/**
 * Discard all samples from the buffer that come before a specified
 * absolute waveform position.
 * 
 * This function can be used to discard samples that are not needed anymore
 * in the sample buffer. "wpos" specify the absolute position of the first
 * sample that should be kept: all samples that come before will be discarded.
 * This function will silently do nothing if there are no samples to discard.
 * 
 * @param[in]   buf     Sample buffer
 * @param[in]   wpos    Absolute waveform position of the first sample that
 *                      must be kept.
 */
void samplebuffer_discard(samplebuffer_t *buf, int wpos);

/**
 * Flush (reset) the sample buffer to empty status, discarding all samples.
 * 
 * @param[in]   buf     Sample buffer.
 */
void samplebuffer_flush(samplebuffer_t *buf);

/**
 * Close the sample buffer.
 * 
 * After calling close, the sample buffer must be initialized again before
 * using it.
 * 
 * @param[in]   buf     Sample buffer.
 */
void samplebuffer_close(samplebuffer_t *buf);



/*********************************************************************
 *
 * MIXER VIDEOSYNC API
 *
 *********************************************************************/

// Initialize the mixer videosync. This is a set of helper functions
// that help writing code to perfectly synchronize audio and video.
// "fps" is the exact number of frames per second that the application
// will display and thus the frequency at which mixer_videosync_next_frame
// will be called.
//
// NOTE: this function must be called after mixer_init.
void mixer_videosync_init(float fps);

// Returns the minimum suggested buffer size for a stream, as number of
// samples. Streams should make sure to have an internal buffer of samples
// of at least this size to be able to correctly answer the callbacks
// from the mixer core without crackling sound (= forced silence).
//
// If the caller is using SampleBuffer to implement the stream, the value
// returned by this function can be passed to samplebuffer_init().
unsigned int mixer_videosync_suggested_buffer_size(void);

// mixer_videosync_current_frame_samples returns the number of audio
// samples that should be loaded/produced by an audio stream during
// this frame to keep up with the expected speed of playback. 
//
// The number refers to this frame only, and assumes that previous frames'
// samples have already been loaded/produced and buffered.
//
// The number returned by this function will slightly differ from frame
// to frame because the audio sample rate is never an exact dividend of
// the video frame rate. Using a fixed number for all frames would accumulate
// an error that would eventually desynchronize audio and video.
unsigned int mixer_videosync_current_frame_samples(void);

// mixer_videosync_next_frame notifies the videosync engine that the current
// frame is finished. This adjusts the internal counters to acknowledge the
// new point in time.
void mixer_videosync_next_frame(void);

#endif /* MIXER_H */
