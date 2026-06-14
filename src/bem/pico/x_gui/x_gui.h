/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_X_GUI_H
#define B_EM_PICO_X_GUI_H

#include "pico.h"
#include "pico/util/buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

// don't make a full scanvideo impl, we just define a few things to make things less #if-y in the code

struct scanvideo_scanline_buffer {
    uint32_t scanline_id;
    uint16_t *row0;
    uint16_t *row1;
    bool double_height;
    bool half_line;
};

#define PICO_SCANVIDEO_ALPHA_PIN 6u
//#if DRM_PRIME
//#define PICO_SCANVIDEO_PIXEL_RSHIFT 0u
//#define PICO_SCANVIDEO_PIXEL_GSHIFT 6u
//#define PICO_SCANVIDEO_PIXEL_BSHIFT 11u
//#else
#define PICO_SCANVIDEO_PIXEL_RSHIFT 11u
#define PICO_SCANVIDEO_PIXEL_GSHIFT 6u
#define PICO_SCANVIDEO_PIXEL_BSHIFT 0u
//#endif

#define PICO_SCANVIDEO_ALPHA_MASK (1u << PICO_SCANVIDEO_ALPHA_PIN)
#define PICO_SCANVIDEO_PIXEL_FROM_RGB8(r, g, b) ((((b)>>3u)<<PICO_SCANVIDEO_PIXEL_BSHIFT)|(((g)>>3u)<<PICO_SCANVIDEO_PIXEL_GSHIFT)|(((r)>>3u)<<PICO_SCANVIDEO_PIXEL_RSHIFT))
#define PICO_SCANVIDEO_PIXEL_FROM_RGB5(r, g, b) (((b)<<PICO_SCANVIDEO_PIXEL_BSHIFT)|((g)<<PICO_SCANVIDEO_PIXEL_GSHIFT)|((r)<<PICO_SCANVIDEO_PIXEL_RSHIFT))
#define PICO_SCANVIDEO_R5_FROM_PIXEL(p) (((p)>>PICO_SCANVIDEO_PIXEL_RSHIFT)&0x1f)
#define PICO_SCANVIDEO_G5_FROM_PIXEL(p) (((p)>>PICO_SCANVIDEO_PIXEL_GSHIFT)&0x1f)
#define PICO_SCANVIDEO_B5_FROM_PIXEL(p) (((p)>>PICO_SCANVIDEO_PIXEL_BSHIFT)&0x1f)

static inline uint16_t scanvideo_frame_number(uint32_t scanline_id) {
    return (uint16_t) (scanline_id >> 16u);
}

static inline uint16_t scanvideo_scanline_number(uint32_t scanline_id) {
    return (uint16_t) scanline_id;
}

// and audio

#define AUDIO_BUFFER_FORMAT_PCM_S16 1          ///< signed 16bit PCM
#define AUDIO_BUFFER_FORMAT_PCM_S8 2           ///< signed 8bit PCM
#define AUDIO_BUFFER_FORMAT_PCM_U16 3          ///< unsigned 16bit PCM
#define AUDIO_BUFFER_FORMAT_PCM_U8 4           ///< unsigned 16bit PCM

struct audio_buffer_pool {
    uint foo;
};

struct audio_format {
    uint32_t sample_freq;      ///< Sample frequency in Hz
    uint16_t format;           ///< Audio format \ref audio_formats
    uint16_t channel_count;    ///< Number of channels
};

/** \brief Audio buffer format definition
 */
struct audio_buffer_format {
    const struct audio_format *format;      ///< Audio format
    uint16_t sample_stride;                 ///< Sample stride
};

/** \brief Audio buffer definition
 */
struct audio_buffer {
    struct mem_buffer *buffer;
    const struct audio_buffer_format *format;
    uint32_t sample_count;
    uint32_t max_sample_count;
    uint32_t user_data; // only valid while the user has the buffer
    // private - todo make an internal version
    struct audio_buffer *next;
};

// from pico_host_sdl

// todo move these to a host specific header
// todo until we have an abstraction
// These are or'ed with SDL_SCANCODE_* constants in last_key_scancode.
enum key_modifiers {
    WITH_SHIFT = 0x8000,
    WITH_CTRL = 0x4000,
    WITH_ALT = 0x2000,
};
extern void (*platform_key_down)(int scancode, int keysym, int modifiers);
extern void (*platform_key_up)(int scancode, int keysym, int modifiers);
extern void (*platform_mouse_move)(int dx, int dy);
extern void (*platform_mouse_button_down)(int button);
extern void (*platform_mouse_button_up)(int button);
extern void (*platform_quit)();

extern void give_audio_buffer(struct audio_buffer_pool *ac, struct audio_buffer *buffer);
extern struct audio_buffer *take_audio_buffer(struct audio_buffer_pool *ac, bool block);

int x_gui_init();
struct audio_buffer_pool *x_gui_audio_init(uint freq);

struct scanvideo_scanline_buffer *x_gui_begin_scanline();
void x_gui_end_scanline(struct scanvideo_scanline_buffer *buffer);

void x_gui_set_force_aspect_ratio(bool far);
// called during pause
void x_gui_refresh_menu_display();

extern bool x_gui_audio_init_failed;

#ifdef __cplusplus
}
#endif

#endif //B_EM_X_GUI_H
