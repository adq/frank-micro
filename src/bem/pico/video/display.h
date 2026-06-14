/* B-em v2.2 by Tom Walker
 * Pico version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_DISPLAY_H
#define B_EM_PICO_DISPLAY_H

// wip separation of crtc and display

#include "b-em.h"
#include "video_render.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void set_intern_dtype(enum vid_disptype dtype);

#if PICO_ON_DEVICE
#define DISPLAY_WIRE
#endif

#ifndef DISPLAY_WIRE
extern void effect_crtc_reset();
extern void effect_display_reset();
extern void effect_crtc_write(uint reg, uint value);
extern void effect_ula_write(uint reg, uint value);
// todo i think these are all boolean (well vadj > 0)
extern void effect_row_start(int vdispen, int vadj, int interline, int interlline, int cursoron, int sc);
extern void effect_row_end(int hc, bool full_line);
extern void effect_displayed_chars(const uint8_t *dat, int count, int cdraw_pos);

extern void effect_hdisplay_count_pos(bool dispen);
extern void effect_hsync_pos();
// annoying but updated independently of row_start right now
extern void effect_vsync_pos(int interline, int interlline, bool pixelated_pause);
extern void effect_cdraw(int cdraw);
#endif

#if PIXELATED_PAUSE
enum pixelated_pause_state {
    PPS_NONE=0,
    PPS_SETUP_FRAME=1,
    PPS_ACTIVE
};
extern enum pixelated_pause_state cpu_pps;
extern bool clear_pixelated_pause;
extern void pps_frame();
#endif

#ifdef DISPLAY_WIRE

#include "pico/scanvideo.h"

extern void squash_record_buffer(struct scanvideo_scanline_buffer *buffer);

extern struct scanvideo_scanline_buffer *commit_record_buffer(struct scanvideo_scanline_buffer *buffer, bool abort);

enum {
    REC_DRAW_BYTES = 0x0u,
    REC_DISPLAY_RESET = 0x1u,
    REC_CRTC_REG = 0x2u,
    REC_ULA_REG = 0x3u,
    REC_ROW_START = 0x4u,
    REC_ROW_END_FULL = 0x5u,
    REC_ROW_END_HALF = 0x6u,
    REC_HDISPLAY_COUNT_POS = 0x7u,
    REC_HSYNC_POS = 0x8u,
    REC_VSYNC_POS = 0x9u,
    REC_CDRAW = 0xau,
#ifdef USE_CORE1_SOUND
    REC_SN_76489 = 0xbu,
    REC_SOUND_SYNC = 0xcu,
#endif
    REC_BUFFER_WRAP = 0xfe,
    REC_ABORT = 0xff,
};

struct row_start_params {
    union {
        struct {
            uint8_t sc;
            uint8_t vdispen: 1, vadj: 1, interline: 1, interlline: 1, cursoron: 1;
        };
        uint16_t val;
    };
};
static_assert(sizeof(struct row_start_params) == 2, "");

#ifdef USE_CORE1_SOUND
void sound_record_word(uint rec, uint val);
void core1_sound_sync(uint count);
void sn76489_sound_event(uint event);
#endif

#endif

void set_vpos_offset(int o);

#if ENABLE_FRAME_SKIP
void set_frame_skip_count(int c);
#endif

#ifdef __cplusplus
}
#endif
#endif //B_EM_DISPLAY_H
