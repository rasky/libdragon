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

#define AUDIO_TRACE   0

#if AUDIO_TRACE
#define tracef(fmt, ...)  debugf(fmt, ##__VA_ARGS__)
#else
#define tracef(fmt, ...)  ({ })
#endif

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
 * of buffers using #audio_init.  More audio buffers allows for smaller
 * chances of audio glitches but means that there will be more latency
 * in sound output.  When new data is available to be output, code should
 * check to see if there is room in the output buffers using
 * #audio_can_write.  Code can probe the current frequency and buffer
 * size using #audio_get_frequency and #audio_get_buffer_length respectively.
 * When there is additional room, code can add new data to the output
 * buffers using #audio_write.  Be careful as this is a blocking operation,
 * so if code doesn't check for adequate room first, this function will
 * not return until there is room and the samples have been written.
 * When all audio has been written, code should call #audio_close to shut
 * down the audio subsystem cleanly.
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
/** @brief Circular buffer of samples */
static int32_t *buffer = NULL;
/** @brief Size of the circular buffer */
static int buf_size;
/** @brief Read index of the buffer */
static volatile int buf_ridx;
/** @brief Write index of the buffer */
static volatile int buf_widx;
/** @brief Second write index (used to wrap around) */
static volatile int buf_widx2;
/** @brief Buffer is full. */
static volatile bool buf_full;
/** @brief Suggested size of a chunk of audio to be written */
static int suggested_write_size;

static audio_fill_buffer_callback _fill_buffer_callback = NULL;
static audio_fill_buffer_callback _orig_fill_buffer_callback = NULL;

static volatile bool _paused = false;

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
void audio_callback(void)
{
    /* Do not copy more data if we've freed the audio system */
    if(!buffer)
    {
        return;
    }

    /* Disable interrupts so we don't get a race condition with writes */
    disable_interrupts();

    /* Copy in as many buffers as can fit (up to 2) */
    while(!__full())
    {
        tracef("callback: ridx:%x widx:%x widx2:%x\n", buf_ridx, buf_widx, buf_widx2);
        if (buf_widx == buf_ridx)
        {
            if (buf_widx2 == 0)
                break;
            buf_ridx = 0;
            buf_widx = buf_widx2;
            buf_widx2 = 0;
        }
        assert(buf_widx > buf_ridx);

        int nsamples = (buf_widx - buf_ridx);
        assert((nsamples & 1) == 0);

        if (nsamples > buf_size/4)
            nsamples = buf_size/4;

        /* Start the DMA transfer */
        tracef("DMA: %x->%x\n", buf_ridx, buf_ridx+nsamples);
        AI_regs->address = UncachedAddr(buffer + buf_ridx);
        AI_regs->length = nsamples * sizeof(int32_t);
        MEMORY_BARRIER();
        AI_regs->control = 1;

        buf_ridx += nsamples;
        buf_full = false;

        if (_fill_buffer_callback)
        {
            short *out = audio_write_begin(suggested_write_size);
            _fill_buffer_callback((short*)out, suggested_write_size);
            audio_write_end(suggested_write_size);
        }
    }

    /* Safe to enable interrupts here */
    enable_interrupts();
}

/**
 * @brief Initialize the audio DAC.
 * 
 * This is an internal function used by audio.c and mixer.c.
 * Not part of the public libdragon API.
 *
 * @param[in] frequency
 *            The frequency in Hz to play back samples at
 */
void audio_dac_init(const int frequency)
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

    /* Remember frequency */
    AI_regs->dacrate = ((2 * clockrate / frequency) + 1) / 2 - 1;
    AI_regs->samplesize = 15;

    /* Real frequency */
    _frequency = 2 * clockrate / ((2 * clockrate / frequency) + 1);
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
 * @param[in] fill_buffer_callback
 *            A function to be called when more sample data is needed
 */
void audio_init(int frequency, int num_buffers)
{
    /* Initialize the audio DAC */
    audio_dac_init(frequency);

    /* Set up hardware to notify us when it needs more data */
    register_AI_handler(audio_callback);
    set_AI_interrupt(1);

    /* Set up buffers */
    suggested_write_size = CALC_BUFFER(_frequency);
    
    if (num_buffers < 1)
        num_buffers = NUM_BUFFERS;

    buf_size = num_buffers * suggested_write_size + 1;

    buffer = malloc(buf_size * sizeof(int32_t));
    memset(buffer, 0, buf_size * sizeof(int32_t));
    data_cache_hit_writeback_invalidate(buffer, buf_size * sizeof(int32_t));

    /* Set up ring buffer pointers */
    buf_widx = 0;
    buf_widx2 = 0;
    buf_ridx = 0;
    _paused = false;
}

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

    if(buffer)
    {
        free(buffer);
        buffer = 0;
    }
}

static void audio_paused_callback(short *buffer, size_t numsamples)
{
    memset(UncachedShortAddr(buffer), 0, numsamples * sizeof(short) * 2);
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

short* audio_write_begin(int nsamples)
{
    int32_t *out;
    disable_interrupts();
    if (buf_widx2 == 0 && buf_widx + nsamples <= buf_size)
    {
        out = buffer + buf_widx;
        buf_widx += nsamples;
        buf_full = false;
    }
    else
    {
        out = buffer + buf_widx2;
        if (buf_widx2+nsamples > buf_ridx)
            debugf("[audio] buffer is full: ridx:%x widx:%x widx2:%x\n", buf_ridx, buf_widx, buf_widx2);
        else {
            buf_widx2 += nsamples;
            buf_full = buf_widx2 == buf_ridx;
        }
    }
    tracef("audio_write_begin: ridx:%x widx:%x widx2:%x AI:%lx\n", buf_ridx, buf_widx, buf_widx2, ((uint32_t)AI_regs->address - ((uint32_t)buffer & 0x0FFFFFFF)) / 4);
    enable_interrupts();
    return (short*)out;
}

void audio_write_end(int nsamples)
{
    audio_callback();
}


/**
 * @brief Write a chunk of audio data
 *
 * This function takes a chunk of audio data and writes it to an internal
 * buffer which will be played back by the audio system as soon as room
 * becomes available in the AI.  The buffer should contain stereo interleaved
 * samples and be exactly #audio_get_buffer_length stereo samples long.
 *
 * @note This function will block until there is room to write an audio sample.
 *       If you do not want to block, check to see if there is room by calling
 *       #audio_can_write.
 *
 * @param[in] buffer
 *            Buffer containing stereo samples to be played
 */
void audio_write(const short* buffer)
{
    if(!buffer)
        return;

    while(!audio_can_write()) {}
    short *out = audio_write_begin(suggested_write_size);
    memcpy(UncachedShortAddr(out), buffer, suggested_write_size * sizeof(int32_t));
    audio_write_end(suggested_write_size);
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
 */
void audio_write_silence(void)
{
    if(!buffer)
        return;

    while(!audio_can_write()) {}
    short* out = audio_write_begin(suggested_write_size);
    memset(UncachedShortAddr(out), 0, suggested_write_size * sizeof(int32_t));
    audio_write_end(suggested_write_size);
}

/**
 * @brief Return whether there is an empty buffer to write to
 *
 * This function will check to see if there are any buffers that are not full to
 * write data to.  If all buffers are full, wait until the AI has played back
 * the next buffer in its queue and try writing again.
 */
volatile int audio_can_write(void)
{
    return audio_can_write_n(suggested_write_size);
}

volatile bool audio_can_write_n(int nsamples)
{
    return (buf_widx2 == 0 || buf_widx2 + nsamples <= buf_ridx);
}

/**
 * @brief Return actual frequency of audio playback
 *
 * @return Frequency in Hz of the audio playback
 */
int audio_get_frequency(void)
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
int audio_get_buffer_length(void)
{
    return suggested_write_size;
}

/** @} */ /* audio */
