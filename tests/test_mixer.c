
DEFINE_RSP_UCODE(rsp_mixer2);
static uint32_t mxid;

#define INIT_MIXER() \
    rspq_init(); \
    DEFER(rspq_close()); \
    mxid = rspq_overlay_register(&rsp_mixer2); \
    DEFER(rspq_overlay_unregister(mxid));

static void rsp_mixer_begin(int16_t *rdram_buffer, int num_samples, float volume) {
    rspq_write(mxid, 0, 
        PhysicalAddr(rdram_buffer), 
        num_samples | ((int)(volume * 65535.0f) << 16));
}

static void rsp_mixer_end(void) {
    rspq_write(mxid, 1);
}

typedef struct {
    uint32_t pos;                   // Current position (.12)
    uint32_t step;                  // Step between samples (.12)
    uint32_t len;                   // Length of the waveform (in samples)
    uint32_t loop_len;              // Length of the loop (in samples)
    uint32_t rdram_addr;            // Physical address of the start of samples in RDRAM
    uint16_t volume[2];             // Volume setpoint for left and right channel (Q15)
    uint16_t actual_volume[2];      // Current volume used for the left and right channel (Q15)
} rsp_waveform_t;

static void rsp_mixer_resample(rsp_waveform_t *wv)
{
    rspq_write(mxid, 2, PhysicalAddr(wv));
}


void test_mixer_beginend(TestContext *ctx) {
    return;
    INIT_MIXER();
 
    int16_t *samples = malloc_uncached(512 * 2 * sizeof(int16_t) + 16);
    DEFER(free_uncached(samples));
    memset(samples, 0xFF, 512 * 2 * sizeof(int16_t) + 16);

    // Intentienally set a misaligned buffer
    int MISALIGNED_OFFSET = 2;
    rsp_mixer_begin(samples + MISALIGNED_OFFSET, 512, 1.0f);
    rsp_mixer_end();
    rspq_wait();

    // Make sure the data before the misaligned buffer is not modified
    uint64_t begin = *(uint64_t*)samples;
    ASSERT_EQUAL_HEX(begin, 0xFFFFFFFF00000000, "begin of buffer should be 0x0000FFFF");
    ASSERT_EQUAL_HEX(samples[512*2+0], 0, "end of buffer should be 0");
    ASSERT_EQUAL_HEX(samples[512*2+1], 0, "end of buffer should be 0");
}

static int16_t* init_sinewave(int WV_SIZE, int OVERREAD_SAMPLES)
{
    int16_t *sinewave = malloc_uncached((WV_SIZE + 64) * sizeof(int16_t));

    // Create a sine wave
    for (int i=0;i<WV_SIZE;i++) {
        sinewave[i] = (int16_t)(sin(i * 0.1f) * 32767.0f);
    }
    // Fill overread
    for (int i=WV_SIZE;i<WV_SIZE+OVERREAD_SAMPLES;i++) {
        sinewave[i] = 0x00;
    }
    // Fill data that shouldn't be accessed 
    for (int i=WV_SIZE+OVERREAD_SAMPLES;i<WV_SIZE+64;i++) {
        sinewave[i] = 0xAB;
    }
    return sinewave;
}

void test_mixer_noresample(TestContext *ctx) {
    return;
    INIT_MIXER();

    rsp_waveform_t *wv = malloc_uncached(sizeof(rsp_waveform_t));
    DEFER(free_uncached(wv));
    memset(wv, 0, sizeof(rsp_waveform_t));

    const int OUT_SIZE = 128;
    const int WV_SIZE = 480;
    const int OVERREAD_SAMPLES = 8;

    int16_t *samples = malloc_uncached(OUT_SIZE * 2 * sizeof(int16_t) + 16);
    DEFER(free_uncached(samples));

    int16_t *sinewave = init_sinewave(WV_SIZE, OVERREAD_SAMPLES);
    DEFER(free_uncached(sinewave));

    wv->step = 1 << 13;//11857;
    wv->len = WV_SIZE << 13;
    wv->loop_len = 0 << 13;
    wv->rdram_addr = PhysicalAddr(sinewave);
    wv->actual_volume[0] = wv->volume[0] = 32767;  // full volume on left
    wv->actual_volume[1] = wv->volume[1] = 32767>>1; // half volume on right

    // initialize the output buffer with 0xFF. It should get overwritten with
    // data (or 0 if the waveform is finished)
    memset(samples, 0xFF, OUT_SIZE * 2 * sizeof(int16_t) + 16);
    rsp_mixer_begin(samples, OUT_SIZE, 1.0f);
    rsp_mixer_resample(wv);
    rsp_mixer_end();
    rspq_wait();

    // debugf("WAVE:\n");
    // debug_hexdump(sinewave, OUT_SIZE * sizeof(int16_t));
    // for (int i=0; i<WV_SIZE; i++) {
    //     debugf("%d\n", sinewave[i]);
    // }
    debugf("OUT:\n");
    // for (int i=0; i<OUT_SIZE; i++) {
    //     debugf("%d\n", samples[i*2+0]);
    // }
    debug_hexdump(samples, OUT_SIZE * 2 * sizeof(int16_t));

    // Check the the output buffer contains the stereo output of the input
    // sine wave
    for (int i=0; i<OUT_SIZE; i++) {
        int16_t left = samples[i*2+0];
        int16_t right = samples[i*2+1];
        int16_t expected = (i<WV_SIZE) ? sinewave[i] : 0;
        if (abs(left - expected) > 1) {
            ASSERT(0, "Left sample at %d: %d, expected: %d\n", i, left, expected);
        }
        if (abs(right*2 - expected) > 3) {
            ASSERT(0, "Right sample at %d: %d, expected: %d\n", i, right, expected);
        }
    }

    int ns = OUT_SIZE > WV_SIZE ? WV_SIZE : OUT_SIZE;
    ASSERT_EQUAL_HEX(wv->pos, ns<<13, "invalid output position");
}

void test_mixer_resample(TestContext *ctx) {
    INIT_MIXER();

    rsp_waveform_t *wv = malloc_uncached(sizeof(rsp_waveform_t));
    DEFER(free_uncached(wv));
    memset(wv, 0, sizeof(rsp_waveform_t));

    const int OUT_SIZE = 512;
    const int WV_SIZE = 480;
    const int OVERREAD_SAMPLES = 8;

    int16_t *samples = malloc_uncached(OUT_SIZE * 2 * sizeof(int16_t) + 16);
    DEFER(free_uncached(samples));

    int16_t *sinewave = init_sinewave(WV_SIZE, OVERREAD_SAMPLES);
    DEFER(free_uncached(sinewave));

    wv->step = 11857;
    wv->len = WV_SIZE << 13;
    wv->loop_len = 0 << 13;
    wv->rdram_addr = PhysicalAddr(sinewave);
    wv->actual_volume[0] = wv->volume[0] = 32767;  // full volume on left
    wv->actual_volume[1] = wv->volume[1] = 32767; // half volume on right

    // initialize the output buffer with 0xFF. It should get overwritten with
    // data (or 0 if the waveform is finished)
    memset(samples, 0xFF, OUT_SIZE * 2 * sizeof(int16_t) + 16);
    rsp_mixer_begin(samples, OUT_SIZE, 1.0f);
    rsp_mixer_resample(wv);
    rsp_mixer_end();
    rspq_wait();


    // debugf("WAVE:\n");
    // debug_hexdump(sinewave, OUT_SIZE * sizeof(int16_t));
    // for (int i=0; i<WV_SIZE; i++) {
    //     debugf("%d\n", sinewave[i]);
    // }
    debugf("OUT:\n");
    for (int i=0; i<OUT_SIZE; i++) {
        debugf("%d\n", samples[i*2+0]);
    }
    // debug_hexdump(samples, OUT_SIZE * 2 * sizeof(int16_t));
}

