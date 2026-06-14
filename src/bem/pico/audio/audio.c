/* B-em v2.2 by Tom Walker
 * Pico version (C) 2021 Graham Sanderson
 * Internal SN sound chip emulation*/

#include <stdio.h>

#include "b-em.h"
#include "sid_b-em.h"
#include "sn76489.h"
#include "sound.h"
#include "via.h"
#include "uservia.h"
#include "music5000.h"
#include "pico/types.h"
#include "pico/util/buffer.h"

#ifndef X_GUI
#include "pico/audio_i2s.h"
#else
#include "x_gui.h"
#endif

#include "xip_stream.h"
#include "pico/video/display.h"

#include <allegro5/allegro_audio.h>
#include "pico/binary_info.h"

#if 0
#define audio_assert(x) ({if (!(x)) panic("%d", __LINE__);})
#else
#define audio_assert(x) assert(x)
#endif

#ifdef USE_MEM_DDNOISE
#ifndef USE_DDNOISE_8
#include "ddnoise_samples.h"
#else
#include "ddnoise_samples8.h"
#endif
#endif

int8_t disc_volume;

static struct audio_format audio_format = {
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .sample_freq = FREQ_SO,
        .channel_count = 1,
};

#ifndef X_GUI
static struct audio_buffer_format producer_format = {
        .format = &audio_format,
        .sample_stride = 2
};
#endif

#ifdef USE_DDNOISE_8
// we don't really need these, but makes things consistent and future proof a bit
static struct audio_format audio_format8 = {
        .format = AUDIO_BUFFER_FORMAT_PCM_S8,
        .sample_freq = 22050,
        .channel_count = 1,
};

static struct audio_buffer_format producer_format8 = {
        .format = &audio_format8,
        .sample_stride = 1
};
#endif

bool sound_internal = false, sound_beebsid = false, sound_dac = false;
bool sound_ddnoise = false, sound_tape = false;
bool sound_music5000 = false, sound_filter = false;

static int sound_pos = 0;

struct ALLEGRO_SAMPLE {
    char *filename;
    struct audio_buffer *buffer;
#ifdef USE_DDNOISE_8
    uint8_t volume;
#endif
};

struct ALLEGRO_VOICE {
    uint dummy;
};

struct ALLEGRO_AUDIO_STREAM {
    ALLEGRO_EVENT_SOURCE event_source;
};

struct ALLEGRO_MIXER {
    uint dummy;
};

static struct audio_buffer_pool *producer_pool;

#define MAX_PLAYING_SAMPLES 3
#define SAMPLE_BUFFER_LENGTH 128

static struct playing_sample {
    ALLEGRO_SAMPLE *sample;
    struct xip_stream_dma dma;
    uint8_t buffer[2][SAMPLE_BUFFER_LENGTH];
    int buffer_offset[2];
    int next_offset;
    // todo uint8_t?
    uint16_t buffer_length[2];
    uint16_t buffer_pos;
    int8_t playing_buffer;
    int8_t filling_buffer; // or -1
    bool on;
    bool loop;
    uint8_t id;
} playing_samples[MAX_PLAYING_SAMPLES];

#ifdef USE_HW_EVENT
#define SOUND_CYCLES 128 * 128

static bool sound_invoke(struct hw_event *event);

static struct hw_event sound_event = {
        .invoke = sound_invoke
};

#ifdef USE_CORE1_SOUND
#include "display.h"
#endif

int sound_cycle_sync() {
    uint32_t delta = get_hardware_timestamp() - sound_event.user_time;
    assert(delta < 0x7fffffff);
    uint n = delta >> 7u;
    if (n > 0) {
//        printf("Catchup %d\n", n);
#ifndef USE_CORE1_SOUND
        sound_poll_n(n);
#else
        sound_record_word(REC_SOUND_SYNC, n);
#endif
        sound_event.user_time += n * 128;
    }
    return delta & 127;
}

bool sound_invoke(struct hw_event *event) {
    sound_cycle_sync();
    sound_event.target += SOUND_CYCLES;
    return true;
}

#endif

bool al_install_audio(void) {
#if !X_GUI
    producer_pool = audio_new_producer_pool(&producer_format, 3, BUFLEN_SO * 2); // todo correct size
    bool __unused ok;
    // todo bi_decl should be available anyway
#if !PICO_NO_BINARY_INFO
    bi_decl(bi_3pins_with_names(PICO_AUDIO_I2S_DATA_PIN, "I2S DATA", PICO_AUDIO_I2S_CLOCK_PIN_BASE, "I2S BCLK",
                                PICO_AUDIO_I2S_CLOCK_PIN_BASE+1, "I2S LRCLK"));
#endif
    struct audio_i2s_config config = {
            .data_pin = PICO_AUDIO_I2S_DATA_PIN,
            .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
            .dma_channel = 6,
            .pio_sm = 0,
    };

    const struct audio_format *output_format;
#if !FORCE_44100_OUTPUT
output_format = audio_i2s_setup(&audio_format, &config);
#else
static struct audio_format output_audio_format = {
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .sample_freq = 44100,
        .channel_count = 1,
};
output_format = audio_i2s_setup(&output_audio_format, &config);
#endif

    if (!output_format) {
        panic("MuAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(producer_pool);
    assert(ok);
    audio_i2s_set_enabled(true);

#else
    producer_pool = x_gui_audio_init(audio_format.sample_freq);
#endif
    return true;
}

bool al_reserve_samples(int reserve_samples) {
    return true;
}

bool al_init_acodec_addon(void) {
    return true;
}

ALLEGRO_SAMPLE *al_load_sample(const char *filename) {
#ifndef USE_MEM_DDNOISE
    ALLEGRO_SAMPLE *s = (ALLEGRO_SAMPLE *)calloc(1, sizeof(struct ALLEGRO_SAMPLE));
    s->filename = strdup(filename);
    FILE *f  = fopen(s->filename, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f) - 44;
        fseek(f, 44, SEEK_SET);
        int asize = ((int)size / 2)  * FREQ_SO / 22050;
        s->buffer = audio_new_buffer(&producer_format, asize);
        assert(audio_format.format == AUDIO_BUFFER_FORMAT_PCM_S16);
        char *buf = malloc(size);
        fread(buf, size, 1, f);
        audio_upsample((int16_t *)buf, (int16_t *)s->buffer->buffer->bytes, asize, (22050 << 12) / FREQ_SO);
#if 1
        {
            const char *fn1 = strrchr(filename, '.');
            const char *fn2 = strrchr(filename, '/');
            if (fn2) fn2++;
            if (fn1 < fn2) fn1 = fn2 + strlen(fn2);
            char *afn = malloc(fn1 - fn2 + 1);
            strncpy(afn, fn2, fn1 - fn2);
            afn[fn1 - fn2] = 0;
            for(int i=0;i<fn1-fn2;i++) if (afn[i] == '-') afn[i] = '_';
            uint len = s->buffer->sample_count;
            int max = 0;
            int16_t *samples = (int16_t*)s->buffer->buffer->bytes;
            int sample_count = asize;
            for(int i=0;i<sample_count;i++) {
                if (samples[i] > max) max = samples[i];
                else if (-samples[i] > max) max = -samples[i];
            }
            printf("static const int8_t samples_%s[%d] = {\n", afn, sample_count);
            for(uint i=0;i<sample_count;i+=2) {
                printf("    ");
                for(uint j=i;j<MIN(sample_count, i+32);j++) {
                    printf("%d, ", samples[j] * 127 / max);
                }
                printf("\n");
            }
            printf("};\n\n");
            printf("static const struct mem_buffer mem_buffer_%s = {\n", afn);
            printf("   .bytes = (const uint8_t *)&samples_%s,\n", afn);
            printf("   .size = %d,\n", sample_count);
            printf("};\n\n");
            printf("static struct audio_buffer audio_buffer_%s;\n", afn);
            printf("static const uint8_t vol_%s = %d;\n", afn, max / 128);
            free(afn);
        }
#endif
        int vol = 64;
        for(int i=0;i<asize;i++) {
            ((int16_t*)s->buffer->buffer->bytes)[i] = (((int16_t*)s->buffer->buffer->bytes)[i] * vol) / 256;
        }
//        int m = 0;
//        int p = 0;
//        // hacky downsample for now
//        static_assert(FREQ_DD > FREQ_SO, "");
//        uint vol = 140;
//        for(int i=0;i<asize;i++) {
//            ((int16_t *)s->buffer->buffer->bytes)[i] = (vol * ((int16_t *)buf)[p])/256;
//            m += FREQ_DD;
//            while (m >= 0) {
//                p++;
//                m -= FREQ_SO;
//            }
//        }
        free(buf);
        s->buffer->sample_count = asize;
        fclose(f);
#if 0
        const char *fn1 = strrchr(filename, '.');
        const char *fn2 = strrchr(filename, '/');
        if (fn2) fn2++;
        if (fn1 < fn2) fn1 = fn2 + strlen(fn2);
        char *afn = malloc(fn1 - fn2 + 1);
        strncpy(afn, fn2, fn1 - fn2);
        afn[fn1 - fn2] = 0;
        for(int i=0;i<fn1-fn2;i++) if (afn[i] == '-') afn[i] = '_';
        uint len = s->buffer->sample_count;
        printf("static const int16_t samples_%s[%d] = {\n", afn, len);
        for(uint i=0;i<len;i+=32) {
            printf("    ");
            for(uint j=i;j<MIN(len, i+32);j++) {
                printf("%d, ", ((int16_t*)s->buffer->buffer->bytes)[j]);
            }
            printf("\n");
        }
        printf("};\n\n");
        printf("static const struct mem_buffer mem_buffer_%s = {\n", afn);
        printf("   .bytes = (const uint8_t *)&samples_%s,\n", afn);
        printf("   .size = %d,\n", len * 2);
        printf("};\n\n");
        printf("static struct audio_buffer audio_buffer_%s;\n", afn);
        free(afn);
#endif
    }
#else

    const struct mem_buffer *buffer = NULL;
    uint vol = 256;
    if (!strcmp(filename, "step")) {
        buffer = &mem_buffer_step;
        vol = vol_step;
    } else if (!strcmp(filename, "seek")) {
        buffer = &mem_buffer_seek;
        vol = vol_seek;
    } else if (!strcmp(filename, "seek2")) {
        buffer = &mem_buffer_seek2;
        vol = vol_seek2;
    } else if (!strcmp(filename, "seek3")) {
        buffer = &mem_buffer_seek3;
        vol = vol_seek3;
    } else if (!strcmp(filename, "motoron")) {
        buffer = &mem_buffer_motoron;
        vol = vol_motoron;
    } else if (!strcmp(filename, "motor")) {
        buffer = &mem_buffer_motor;
        vol = vol_motor;
    } else if (!strcmp(filename, "motoroff")) {
        buffer = &mem_buffer_motoroff;
        vol = vol_motoroff;
    }
    ALLEGRO_SAMPLE *s;
    if (buffer) {
        s = (ALLEGRO_SAMPLE *) calloc(1, sizeof(struct ALLEGRO_SAMPLE));
        s->buffer = (struct audio_buffer *) calloc(1, sizeof(struct audio_buffer));
        s->buffer->buffer = (struct mem_buffer *) buffer;
#ifndef USE_DDNOISE_8
        s->buffer->sample_count = s->buffer->max_sample_count = buffer->size / 2;
        s->buffer->format = &producer_format;
#else
        s->buffer->sample_count = s->buffer->max_sample_count =
                buffer->size & ~3; // hack to avoid faffing with non words in xip stream
        s->buffer->format = &producer_format8;
        s->volume = vol;
#endif
    } else {
        s = NULL;
    }

#endif

    return s;
}

unsigned int al_get_sample_frequency(const ALLEGRO_SAMPLE *spl) {
    return FREQ_SO;
}

unsigned int al_get_sample_length(const ALLEGRO_SAMPLE *spl) {
    return spl->buffer->sample_count;
}

static void start_filling(struct playing_sample *s) {
    assert(s->filling_buffer == -1);
    s->filling_buffer = s->playing_buffer >= 0 ? s->playing_buffer ^ 1 : 0;
    uint length = SAMPLE_BUFFER_LENGTH;
    if (length > s->sample->buffer->sample_count - s->next_offset) {
        length = s->sample->buffer->sample_count - s->next_offset;
    }
    s->buffer_offset[s->filling_buffer] = s->next_offset;
    s->buffer_length[s->filling_buffer] = length;
//    printf("%d %p Start filling %d %d +%d dma %d\n", s->id, s, s->filling_buffer, s->next_offset, length, s->dma.state);

    audio_assert(s->dma.state == NONE || s->dma.state == COMPLETE);
    audio_assert(s->dma.next == NULL);
    s->dma.state = NONE;
    s->dma.dest = (uint32_t *) &s->buffer[s->filling_buffer][0];
    s->dma.src = (uint32_t *) (s->sample->buffer->buffer->bytes + s->next_offset);
    audio_assert(!(length & 3));
    s->dma.transfer_size = length / 4;
    xip_stream_dma_start(&s->dma);
}

struct sample_access {
    const uint8_t *buffer;
    uint length;
};

static void peek_samples(struct playing_sample *s, struct sample_access *access) {
    if (s->playing_buffer < 0) {
        assert(s->filling_buffer >= 0);
//        printf("%d Start playing %d\n", s->id, s->filling_buffer);
        // wait on it to be full
        assert(s->buffer_offset[s->filling_buffer] >= 0);
        assert(s->buffer_length[s->filling_buffer] > 0);
        s->playing_buffer = s->filling_buffer;
        while (s->dma.completed * 4 < s->buffer_length[s->filling_buffer]) {
            xip_stream_dma_poll();
            tight_loop_contents();
        }
        s->next_offset += s->buffer_length[s->filling_buffer];
        s->filling_buffer = -1;
        if (s->next_offset == s->sample->buffer->sample_count) {
            if (s->loop) {
                s->next_offset = 0;
                start_filling(s);
            }
        } else {
            start_filling(s);
        }
        s->buffer_pos = 0;
    }
    access->buffer = s->buffer[s->playing_buffer] + s->buffer_pos;
    access->length = s->buffer_length[s->playing_buffer] - s->buffer_pos;
    // note access->length = 0 allowed for end of sample
//    printf("%d Continue playing %d : %d\n", s->id, s->playing_buffer, s->buffer_pos);
}

static void skip_samples(struct playing_sample *s, uint count) {
    assert(s->buffer_offset >= 0);
    assert(s->playing_buffer >= 0);
    assert(s->buffer_offset[s->playing_buffer] + s->buffer_pos + count <= s->sample->buffer->sample_count);
    assert(s->buffer_pos + count <= s->buffer_length[s->playing_buffer]);
    s->buffer_pos += count;
    if (s->buffer_pos == s->buffer_length[s->playing_buffer]) {
        uint next = s->buffer_offset[s->playing_buffer] + s->buffer_length[s->playing_buffer];
        if (next == s->sample->buffer->sample_count) {
            //                            printf("  sample ends (loop = %d)\n", playing_samples[i].loop);
            if (!s->loop) {
                s->on = false;
                return;
            }
        }
        s->playing_buffer = -1;
    }
}

bool al_play_sample(ALLEGRO_SAMPLE *data,
                    float gain, float pan, float speed, ALLEGRO_PLAYMODE loop, ALLEGRO_SAMPLE_ID *ret_id) {
//    printf("Play sample %s\n", data->filename);
    int pos = -1;
    for (int i = 0; i < MAX_PLAYING_SAMPLES; i++) {
        struct playing_sample *s = playing_samples + i;
        if (!s->on) {
            s->sample = data;
            s->loop = loop == ALLEGRO_PLAYMODE_LOOP;
            s->buffer_offset[0] = s->buffer_offset[1] = -1;
            s->playing_buffer = s->filling_buffer = -1;
            s->next_offset = 0;
            s->on = true;
            static int id;
            s->id = ++id;
            start_filling(s);
            pos = i;
            break;
        }
    }
    if (pos == -1) {
        printf("TOO MANY SAMPLES\n");
        return false;
    }
    if (ret_id) {
        ret_id->_index = pos;
        ret_id->_id = playing_samples[pos].id;
    }
    return true;
}

void al_stop_sample(ALLEGRO_SAMPLE_ID *spl_id) {
    for (int i = 0; i < MAX_PLAYING_SAMPLES; i++) {
        if (playing_samples[i].on && playing_samples[i].id == spl_id->_id) {
//            printf("Ending sample %p\n", playing_samples + i);
            xip_stream_dma_cancel(&playing_samples[i].dma);
            playing_samples[i].on = false;
            break;
        }
    }
}

void al_destroy_sample(ALLEGRO_SAMPLE *spl) {
    if (spl) {
        // mu_buffer free?
        free(spl->filename);
        free(spl);
    }
}

bool al_attach_audio_stream_to_mixer(ALLEGRO_AUDIO_STREAM *stream,
                                     ALLEGRO_MIXER *mixer) {
    return true;
}

bool al_attach_mixer_to_voice(ALLEGRO_MIXER *mixer,
                              ALLEGRO_VOICE *voice) {
    return true;
}

ALLEGRO_VOICE *al_create_voice(unsigned int freq,
                               ALLEGRO_AUDIO_DEPTH depth,
                               ALLEGRO_CHANNEL_CONF chan_conf) {
    ALLEGRO_VOICE *voice = malloc(sizeof(ALLEGRO_VOICE));
    return voice;
}

void al_destroy_voice(ALLEGRO_VOICE *voice) {
    free(voice);
}

ALLEGRO_AUDIO_STREAM *al_create_audio_stream(size_t buffer_count,
                                             unsigned int samples, unsigned int freq,
                                             ALLEGRO_AUDIO_DEPTH depth, ALLEGRO_CHANNEL_CONF chan_conf) {
    ALLEGRO_AUDIO_STREAM *stream = malloc(sizeof(ALLEGRO_AUDIO_STREAM));
    return stream;
}

void al_destroy_audio_stream(ALLEGRO_AUDIO_STREAM *stream) {
    free(stream);
}

ALLEGRO_MIXER *al_create_mixer(unsigned int freq,
                               ALLEGRO_AUDIO_DEPTH depth, ALLEGRO_CHANNEL_CONF chan_conf) {
    ALLEGRO_MIXER *mixer = malloc(sizeof(ALLEGRO_MIXER));
    return mixer;
}

void al_destroy_mixer(ALLEGRO_MIXER *mixer) {
    free(mixer);
}

ALLEGRO_EVENT_SOURCE *al_get_audio_stream_event_source(ALLEGRO_AUDIO_STREAM *stream) {
    return &stream->event_source;
}

void *al_get_audio_stream_fragment(const ALLEGRO_AUDIO_STREAM *stream) {
    // todo this is take free buffer
    return NULL;
}

bool al_set_audio_stream_fragment(ALLEGRO_AUDIO_STREAM *stream, void *val) {
    // todo this is give full buffer
    return true;
}

bool al_set_audio_stream_playing(ALLEGRO_AUDIO_STREAM *stream, bool val) {
    printf("set audio sector_read playing %d\n", val);
    return true;
}

#ifndef USE_DDNOISE_8
static void mix_samples(int16_t *dest, int16_t *src, int count, int depth) {
    if (!depth) {
        memcpy(dest, src, count * 2);
    } else {
        for (int i = 0; i < count; i++) {
            dest[i] += src[i];
        }
    }
}
#else
static void mix_samples8(int16_t *dest, int8_t *src, int count, int depth, int vol) {
    if (!depth) {
        for (int i = 0; i < count; i++) {
            dest[i] = src[i] * vol;
        }
    } else {
        for (int i = 0; i < count; i++) {
            dest[i] += src[i] * vol;
        }
    }
}
#endif

void sound_poll_n(int n) {
    if ((sound_internal || sound_beebsid) && producer_pool) {
        while (n > 0) {
            static struct audio_buffer *buffer;
            if (!buffer) {
                buffer = take_audio_buffer(producer_pool, true);
                assert(!(buffer->max_sample_count & 1)); // just simplify code below a little
                int playing_count = 0;
                for (int i = 0; i < MAX_PLAYING_SAMPLES; i++) {
                    if (playing_samples[i].on) {
                        int pos = 0;
                        do {
                            struct sample_access access;
                            peek_samples(playing_samples + i, &access);
                            int to_play = MIN(access.length,
                                              buffer->max_sample_count - pos);
                            //                        printf("%d pos %d/%d to_play %d sample_pos %d/%d\n",  playing_count, pos, buffer->max_sample_count, to_play, playing_samples[i].pos,
                            //                                patch->sample_count
                            //                        );
                            if (!to_play) break;
                            if (disc_volume) {
#ifndef USE_DDNOISE_8
                                mix_samples((int16_t *) buffer->buffer->bytes + pos,
                                            (int8_t *) access.buffer, to_play,
                                            playing_count);
#else
                                mix_samples8((int16_t *) buffer->buffer->bytes + pos,
                                             (int8_t *) access.buffer, to_play,
                                             playing_count,
                                             playing_samples[i].sample->volume * disc_volume / 20);
#endif
                            }
                            skip_samples(playing_samples + i, to_play);
                            pos += to_play;
                        } while (pos != buffer->max_sample_count);
                        if (pos != buffer->max_sample_count && !playing_count) {
                            memset((int16_t *) buffer->buffer->bytes + pos, 0, (buffer->max_sample_count - pos) * 2);
                        }
                        playing_count++;
                    }
                }
                if (!playing_count || !disc_volume) {
                    memset(buffer->buffer->bytes, 0, buffer->buffer->size);
                }
            }
            int16_t *sound_buffer = (int16_t *) buffer->buffer->bytes;
#ifndef NO_USE_SID
            if (sound_beebsid)
                    sid_fillbuf(sound_buffer + sound_pos, buffer->max_sample_count);
#endif
            int len = MIN(2 * n, buffer->max_sample_count - sound_pos);
            assert(!(len & 1));
            if (sound_internal)
                sn_fillbuf(sound_buffer + sound_pos, len);

            sound_pos += len;
            if (sound_pos == buffer->max_sample_count) {
                buffer->sample_count = buffer->max_sample_count;
                give_audio_buffer(producer_pool, buffer);
                buffer = NULL;
                sound_pos = 0;
            } else {
                assert(sound_pos < buffer->max_sample_count);
            }
            n -= len / 2;
        }
    }
}

#ifndef USE_HW_EVENT
void sound_poll(void) {
    sound_poll_n(1);
}
int sound_cycle_sync() {
    return 0;
}
#endif

void sound_init(void) {
#ifdef USE_HW_EVENT
    sound_event.user_time = sound_event.target = get_hardware_timestamp();
    upsert_hw_event(&sound_event);
#endif
}

void sound_close(void) {
}

void set_sound_volume(int vol) {

}

void set_disc_volume(int vol) {
    disc_volume = vol;
}

#ifdef USE_CORE1_SOUND
void core1_sound_sync(uint count) {
    sound_poll_n(count);
}
#endif
