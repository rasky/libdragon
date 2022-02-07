/**
 * @file audio.c
 * @brief Audio Subsystem
 * @ingroup audio
 */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "libdragon.h"
#include "regsinternal.h"
#include "n64sys.h"
#include "utils.h"

/**
 * @defgroup audio Audio Subsystem
 * @ingroup libdragon
 * @brief Interface to the N64 audio hardware.
 *
 * The audio subsystem handles queueing up chunks of audio data for
 * playback using the N64 audio DAC.  The audio subsystem handles
 * DMAing chunks of data to the audio DAC as well as audio callbacks
 * when there is room for another chunk to be written.  Buffer size
 * is calculated automatically based on the requested audio frequency.
 * The audio subsystem accomplishes this by interfacing with the audio
 * interface (AI) registers.
 *
 * Because the audio DAC is timed off of the system clock of the N64,
 * the audio subsystem needs to know what region the N64 is from.  This
 * is due to the fact that the system clock is timed differently for
 * PAL, NTSC and MPAL regions.  This is handled automatically by the
 * audio subsystem based on settings left by the bootloader.
 *
 * Code attempting to output audio on the N64 should initialize the
 * audio subsystem at the desired frequency and with the desired number
 * of buffers using #audio_init.  More audio buffers allow for smaller
 * chances of audio glitches but means that there will be more latency
 * in sound output. 
 * 
 * When new data is available to be output, code should
 * call #audio_push, which is the simplest, highest-level way to put
 * samples into the audio buffers. #audio_push can either block until all
 * samples have been written (potentially waiting for the AI to playback
 * and release some buffers), or copy as many samples as possible and
 * exit without blocking, returning the number of copied samples to the
 * caller. This API is very easy to use, but it forces all samples to
 * be copied once into the audio buffers.
 * 
 * Alternatively, a lower-level set of APIs is available, which is
 * harder to use, but allows for more performance. It is possible
 * to call #audio_write_begin to acquire a pointer to an internal audio
 * buffer, that must be then fully filled with samples (the size of
 * the buffers can be read with #audio_get_buffer_length). This means
 * that the calling code will have to call the function only when at
 * least those number of samples are available to be written.
 * When the samples have been written, code must call #audio_write_end
 * to notify the audio library that the buffer is now ready for playback.
 * To achieve higher performance, calling code can arrange to generate
 * samples directly within the audio buffer returned by #audio_write_begin,
 * to avoid having to copy them, reducing memory bandwidth.
 * #audio_write_begin will block until a buffer is available. It is possible
 * to call #audio_can_write to probe whether at least one buffer is
 * available.
 * 
 * @{
 */

/**
 * @name DAC rates for different regions
 * @{
 */
/** @brief NTSC DAC rate */
#define AI_NTSC_DACRATE 48681812
/** @brief PAL DAC rate */
#define AI_PAL_DACRATE  49656530
/** @brief MPAL DAC rate */
#define AI_MPAL_DACRATE 48628316
/** @} */

/**
 * @name AI Status Register Values
 * @{
 */
/** @brief Bit representing that the AI is busy */
#define AI_STATUS_BUSY  ( 1 << 30 )
/** @brief Bit representing that the AI is full */
#define AI_STATUS_FULL  ( 1 << 31 )
/** @} */

/** @brief Number of buffers the audio subsytem allocates and manages */
#define NUM_BUFFERS     4
/**
 * @brief Macro that calculates the size of a buffer based on frequency
 *
 * @param[in] x
 *            Frequency the AI is running at
 *
 * @return The size of the buffer in bytes rounded to an 8 byte boundary
 */
#define CALC_BUFFER(x)  ( ( ( ( x ) / 25 ) >> 3 ) << 3 )

/** @brief The actual frequency the AI will run at */
static int _frequency = 0;
/** @brief The number of buffers currently allocated */
static int _num_buf = NUM_BUFFERS;
/** @brief The buffer size in bytes for each buffer allocated */
static int _buf_size = 0;
/** @brief Array of pointers to the allocated buffers */
static short **buffers = NULL;

static audio_fill_buffer_callback _fill_buffer_callback = NULL;
static audio_fill_buffer_callback _orig_fill_buffer_callback = NULL;

static volatile bool _paused = false;

/** @brief Index of the current playing buffer */
static volatile int now_playing = 0;
/** @brief Length of the playing queue (number of buffers queued for AI DMA) */
static volatile int playing_queue = 0;
/** @brief Index of the last buffer that has been emptied (after playing) */
static volatile int now_empty = 0;
/** @brief Index pf the currently being written buffer */
static volatile int now_writing = 0;
/** @brief Bitmask of buffers indicating which buffers are full */
static volatile int buf_full = 0;

/** @brief Structure used to interact with the AI registers */
static volatile struct AI_regs_s * const AI_regs = (struct AI_regs_s *)0xa4500000;

/**
 * @brief Return whether the AI is currently busy
 *
 * @return nonzero if the AI is busy, zero otherwise
 */
static volatile inline int __busy()
{
    return AI_regs->status & AI_STATUS_BUSY;
}

/**
 * @brief Return whether the AI is currently full
 *
 * @return nonzero if the AI is full, zero otherwise
 */
static volatile inline int __full()
{
    return AI_regs->status & AI_STATUS_FULL;
}

/**
 * @brief Send next available chunks of audio data to the AI
 *
 * This function is called whenever internal buffers are running low.  It will
 * send as many buffers as possible to the AI until the AI is full.
 */
static void audio_callback()
{
    /* Do not copy more data if we've freed the audio system */
    if(!buffers)
    {
        return;
    }

    /* Disable interrupts so we don't get a race condition with writes */
    disable_interrupts();

    /* Check how many queued buffers were consumed, and update buf_full flags
       accordingly, to make them available for further writes. */
    uint32_t status = AI_regs->status;
    if (playing_queue > 1 && !(status & AI_STATUS_FULL)) {
        playing_queue--;
        now_empty = (now_empty + 1) % _num_buf;
        buf_full &= ~(1<<now_empty);
    }
    if (playing_queue > 0 && !(status & AI_STATUS_BUSY)) {
        playing_queue--;
        now_empty = (now_empty + 1) % _num_buf;
        buf_full &= ~(1<<now_empty);
    }

    /* Copy in as many buffers as can fit (up to 2) */
    while(playing_queue < 2)
    {
        /* check if next buffer is full */
        int next = (now_playing + 1) % _num_buf;
        if ((!(buf_full & (1<<next))) && !_fill_buffer_callback)
        {
            break;
        }

        if (_fill_buffer_callback) {
            _fill_buffer_callback(buffers[next], _buf_size);
        }

        /* Enqueue next buffer. Don't mark it as empty right now because the
           DMA will run in background, and we need to avoid audio_write()
           to reuse it before the DMA is finished. */
        AI_regs->address = buffers[next];
        MEMORY_BARRIER();
        AI_regs->length = (_buf_size * 2 * 2 ) & ( ~7 );
        MEMORY_BARRIER();

        /* Start DMA */
        AI_regs->control = 1;
        MEMORY_BARRIER();

        /* Remember that we queued one buffer */
        playing_queue++;
        now_playing = next;
    }

    /* Safe to enable interrupts here */
    enable_interrupts();
}


/**
 * @brief Initialize the audio subsystem
 *
 * This function will set up the AI to play at a given frequency and
 * allocate a number of back buffers to write data to.
 *
 * @note Before re-initializing the audio subsystem to a new playback
 *       frequency, remember to call #audio_close.
 *
 * @param[in] frequency
 *            The frequency in Hz to play back samples at
 * @param[in] numbuffers
 *            The number of buffers to allocate internally
 */
void audio_init(const int frequency, int numbuffers)
{
    int clockrate;

    switch (get_tv_type())
    {
        case TV_PAL:
            /* PAL */
            clockrate = AI_PAL_DACRATE;
            break;
        case TV_MPAL:
            /* MPAL */
            clockrate = AI_MPAL_DACRATE;
            break;
        case TV_NTSC:
        default:
            /* NTSC */
            clockrate = AI_NTSC_DACRATE;
            break;
    }

    /* Make sure we don't choose too many buffers */
    if( numbuffers > (sizeof(buf_full) * 8) )
    {
        /* This is a bit mask, so we can only have as many
         * buffers as we have bits. */
        numbuffers = sizeof(buf_full) * 8;
    }

    /* Remember frequency */
    AI_regs->dacrate = ((2 * clockrate / frequency) + 1) / 2 - 1;
    AI_regs->samplesize = 15;

    /* Real frequency */
    _frequency = 2 * clockrate / ((2 * clockrate / frequency) + 1);

    /* Set up hardware to notify us when it needs more data */
    register_AI_handler(audio_callback);
    set_AI_interrupt(1);

    /* Set up buffers */
    _buf_size = CALC_BUFFER(_frequency);
    _num_buf = (numbuffers > 1) ? numbuffers : NUM_BUFFERS;
    buffers = malloc(_num_buf * sizeof(short *));

    for(int i = 0; i < _num_buf; i++)
    {
        /* Stereo buffers, interleaved */
        buffers[i] = malloc_uncached(sizeof(short) * 2 * _buf_size);
        memset(buffers[i], 0, sizeof(short) * 2 * _buf_size);
    }

    /* Set up ring buffer pointers */
    now_playing = 0;
    now_empty = 0;
    now_writing = 0;
    buf_full = 0;
    _paused = false;
}

/**
 * @brief Install a audio callback to fill the audio buffer when required.
 * 
 * This function allows to implement a pull-based audio system. It registers
 * a callback which will be invoked under interrupt whenever the AI is ready
 * to have more samples enqueued. The callback can fill the provided audio
 * data with samples that will be enqueued for DMA to AI.
 * 
 * @param[in] fill_buffer_callback   Callback to fill an empty audio buffer
 */
void audio_set_buffer_callback(audio_fill_buffer_callback fill_buffer_callback)
{
    disable_interrupts();
    _orig_fill_buffer_callback = fill_buffer_callback;
    if (!_paused) {
        _fill_buffer_callback = fill_buffer_callback;
    }
    enable_interrupts();
}

/**
 * @brief Close the audio subsystem
 *
 * This function closes the audio system and cleans up any internal
 * memory allocated by #audio_init.
 */
void audio_close()
{
    set_AI_interrupt(0);
    unregister_AI_handler(audio_callback);

    if(buffers)
    {
        for(int i = 0; i < _num_buf; i++)
        {
            /* Nuke anything that isn't freed */
            if(buffers[i])
            {
                free_uncached(buffers[i]);
                buffers[i] = 0;
            }
        }

        /* Nuke array of buffers we init'd earlier */
        free(buffers);
        buffers = 0;
    }

    _frequency = 0;
    _buf_size = 0;
}

static void audio_paused_callback(short *buffer, size_t numsamples)
{
    memset(buffer, 0, numsamples * sizeof(short) * 2);
}

/**
 * @brief Pause or resume audio playback
 *
 * Should only be used when a fill_buffer_callback has been set
 * in #audio_init.
 * Silence will be generated while playback is paused.
 */
void audio_pause(bool pause) {
    if (pause != _paused && _fill_buffer_callback) {
        disable_interrupts();

        _paused = pause;
        if (pause) {
            _orig_fill_buffer_callback = _fill_buffer_callback;
            _fill_buffer_callback = audio_paused_callback;
        } else {
            _fill_buffer_callback = _orig_fill_buffer_callback;
        }

        enable_interrupts();
	}
}

/**
 * @brief Start writing to the first free internal buffer.
 * 
 * This function returns a pointer to the start of the first free
 * internal buffer, where samples can be written for playback.
 * 
 * The buffer should be filled with stereo interleaved samples, and
 * exactly #audio_get_buffer_length samples should be written. Most
 * code will need to call memcpy to copy samples from a source
 * buffer into the buffer returned by this function. For
 * maximum performance, the caller code can arrange for samples to
 * be generated directly within this buffer, to avoid the memcpy call.
 * 
 * After you have written that exact number of samples, call
 * audio_write_end() to notify the library that the buffer is
 * ready to be sent to AI.
 * 
 * If you need to be able to write any number of samples into audio
 * buffers, see #audio_push that offers a simpler API, though it might
 * be less performant because it always requires to copy samples.
 * 
 * @note This function will block until there is room to write an audio sample.
 *       If you do not want to block, check to see if there is room by calling
 *       #audio_can_write.
 * 
 * @return  Pointer to the internal memory buffer where to write samples.
 * 
 * @see #audio_push
 */
short* audio_write_begin(void) 
{
    if(!buffers)
    {
        return NULL;
    }

    disable_interrupts();

    /* check for empty buffer */
    int next = (now_writing + 1) % _num_buf;
    while (buf_full & (1<<next))
    {
        // buffers full
        audio_callback();
        enable_interrupts();
        disable_interrupts();
    }

    /* Copy buffer into local buffers */
    now_writing = next;
    enable_interrupts();

    return buffers[now_writing];
}

/**
 * @brief Complete writing to an internal buffer.
 * 
 * This function is meant to be used in pair with #audio_write_begin.
 * Call this once you have generated the samples, so that the audio
 * system knows the buffer has been filled and can be played back.
 */
void audio_write_end(void)
{
    disable_interrupts();
    buf_full |= (1<<now_writing);
    audio_callback();
    enable_interrupts();
}

/**
 * @brief Write samples into the audio buffers.
 *
 * This function is the highest level way of pushing samples into the audio
 * library. It accepts a buffer of samples (of any length), and handles
 * internally the copy into the low-level fixed-size audio buffers.
 * 
 * Since the function might internally buffer some samples not yet played
 * back, you can use `audio_push(NULL, 0, true)` to flush the buffered
 * samples when you are done with the playback. Silence will be added if
 * required (to pad the audio buffers).
 * 
 * @param[in] buffer
 *            Buffer containing stereo samples to be played back. If NULL,
 *            silence will be inserted into the audio buffers for the specified
 *            number of samples. If NULL, and nsamples is 0, any internal buffer
 *            will be flushed and padded with silence (use this at the end
 *            of playback).
 *
 * @param[in] nsamples
 *            Number of samples in buffer, to be played back.
 *
 * @param[in] blocking
 *            If true, the function will block until the specified samples 
 *            are written into the audio buffers. If false, the function will
 *            write as many samples as possible and exit, returning the number
 *            of written samples.
 *
 * @return    The number of written samples. If blocking is true, this will
 *            always be equal to nsamples, as the function will block until
 *            all samples have been written.
 */
int audio_push(short *buffer, int nsamples, bool blocking)
{
    static short *dst = NULL; static int dst_sz = 0;
    int written = 0;

    if (!buffer && !nsamples)
        nsamples = dst_sz;

    while (nsamples > 0 && (blocking || dst || audio_can_write())) {
        if (!dst) {
            dst = audio_write_begin();
            dst_sz = audio_get_buffer_length();
        }

        int ns = MIN(nsamples, dst_sz);
        if (!buffer)
            memset(dst, 0, ns * 2 * sizeof(short));
        else {
            memcpy(dst, buffer, ns * 2 * sizeof(short));
            buffer += ns*2;
        }

        dst += ns*2;
        nsamples -= ns;
        dst_sz -= ns;
        written += ns;

        if (dst_sz == 0) {
            audio_write_end();
            dst = NULL;
        }
    }

    return written;
}

/**
 * @brief Write a chunk of silence
 *
 * This function will write silence to be played back by the audio system.
 * It writes exactly #audio_get_buffer_length stereo samples.
 *
 * @note This function will block until there is room to write an audio sample.
 *       If you do not want to block, check to see if there is room by calling
 *       #audio_can_write.
 *       
 * @see #audio_write_begin
 * @see #audio_push
 */
void audio_write_silence(void)
{
    short *dst = audio_write_begin();
    memset(dst, 0, _buf_size * 2 * sizeof(short));
    audio_write_end();
}

/**
 * @brief Return whether there is an empty buffer to write to
 *
 * This function will check to see if there are any buffers that are not full to
 * write data to.  If all buffers are full, wait until the AI has played back
 * the next buffer in its queue and try writing again.
 */
volatile int audio_can_write()
{
    if(!buffers)
    {
        return 0;
    }

    /* check for empty buffer */
    int next = (now_writing + 1) % _num_buf;
    return (buf_full & (1<<next)) ? 0 : 1;
}

/**
 * @brief Return actual frequency of audio playback
 *
 * @return Frequency in Hz of the audio playback
 */
int audio_get_frequency()
{
    return _frequency;
}

/**
 * @brief Get the number of stereo samples that fit into an allocated buffer
 *
 * @note To get the number of bytes to allocate, multiply the return by
 *       2 * sizeof( short )
 *
 * @return The number of stereo samples in an allocated buffer
 */
int audio_get_buffer_length()
{
    return _buf_size;
}

/// @cond
// Older APIs. Do not use in new code. Not explicitly deprecated yet.
void audio_write(const short * const buffer)
{
    short* dst = audio_write_begin();
    memcpy(dst, buffer, _buf_size * 2 * sizeof(short));
    audio_write_end();
}
/// @endcond

/** @} */ /* audio */
