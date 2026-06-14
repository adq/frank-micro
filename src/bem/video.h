#ifndef __INC_VIDEO_H
#define __INC_VIDEO_H

#define CLOCKS_PER_FRAME 80000

/*CRTC (6845)*/
void    crtc_reset(void);
void    crtc_write(uint16_t addr, uint8_t val);
uint8_t crtc_read(uint16_t addr);
void    crtc_latchpen(void);
void    crtc_savestate(FILE *f);
void    crtc_loadstate(FILE *f);

#ifndef NO_USE_DEBUGGER
extern uint8_t crtc[32];
extern int crtc_i;

extern int hc, vc, sc;
extern uint16_t ma;
#endif

/*Video ULA (VIDPROC)*/
void videoula_write(uint16_t addr, uint8_t val);
void videoula_savestate(FILE *f);
void videoula_loadstate(FILE *f);

#ifndef NO_USE_DEBUGGER
extern uint8_t ula_ctrl;
extern uint8_t ula_palbak[16];
extern int nula_collook[16];
extern uint8_t nula_flash[8];

extern uint8_t nula_palette_mode;
extern uint8_t nula_horizontal_offset;
extern uint8_t nula_left_blank;
extern uint8_t nula_attribute_mode;
extern uint8_t nula_attribute_text;
#endif
extern uint8_t nula_disable;

ALLEGRO_DISPLAY *video_init(void);
void video_reset(void);
#ifndef USE_HW_EVENT
void video_poll(int clocks, int timer_enable);
#endif
void video_savestate(FILE *f);
void video_loadstate(FILE *f);

void nula_default_palette(void);

void select_vidbank(bool shadow);
void mode7_makechars(void);
#ifndef PICO_BUILD
extern int interlline;
#endif

#ifdef USE_HW_EVENT
void video_cycle_sync();
#endif


enum {
    CRTC_HTOTAL = 0,
    CRTC_HDISPLAY = 1,
    CRTC_HSYNC_POS = 2,
    CRTC_SYNC_WIDTH = 3,

    CRTC_VTOTAL = 4,
    CRTC_VTOTAL_ADJ = 5,
    CRTC_VDISPLAY = 6,
    CRTC_VSYNC_POS = 7,

    CRTC_MODE_CTRL = 8,
    CRTC_CHAR_HEIGHT = 9,

    CRTC_CURSOR_START_CTRL = 10,
    CRTC_CURSOR_END = 11,

    CRTC_DISPLAY_ADDR_H = 12,
    CRTC_DISPLAY_ADDR_L = 13,

    CRTC_CURSOR_ADDR_H = 14,
    CRTC_CURSOR_ADDR_L = 15,

    CRTC_LIGHTPEN_ADDR_H = 16,
    CRTC_LIGHTPEN_ADDR_L = 17,
};

#endif

