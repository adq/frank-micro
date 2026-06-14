/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_MENU_H
#define B_EM_PICO_MENU_H

#include "pico/sync.h"

#ifndef DISPLAY_MENU
#define DISPLAY_MENU (PICO_SCANVIDEO_PLANE_COUNT > 1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if DISPLAY_MENU
#define MENU_GLYPH_MIN 32
#define MENU_GLYPH_COUNT 95
#define MENU_GLYPH_MAX (MENU_GLYPH_MIN + MENU_GLYPH_COUNT - 1)
#define MENU_GLYPH_HEIGHT 9
#define MENU_GLYPH_Y_OFFSET 2
#define MENU_GLYPH_ADVANCE 1

// so we're offset a bit
#define MENU_WIDTH_IN_WORDS 6
#define MENU_LEFT_WIDTH_IN_WORDS 3
#define MENU_RIGHT_WIDTH_IN_WORDS (MENU_WIDTH_IN_WORDS - MENU_LEFT_WIDTH_IN_WORDS)

#define MENU_LINE_HEIGHT 12
// must be multiple of 8
#define MENU_LINE_BORDER_WIDTH 8
// must be multiple of 4
#define MENU_LINE_TEXT_INDENT 16
#define MENU_AREA_WIDTH (MENU_WIDTH_IN_WORDS * 64 + MENU_LINE_TEXT_INDENT * 2 + MENU_LINE_BORDER_WIDTH * 2)
#define MENU_AREA_OFFSET_X ((640 - MENU_AREA_WIDTH)/2)

#ifndef X_GUI
#define MENU_MAX_LINES 8
#else
// X GUI size is dynamic based on the number of menu entries, so we can expand the max
// (note there are a bunch of build var dependent menu items, so having a static number is not ideal)
#define MENU_MAX_LINES 10
#define MENU_MIN_LINES 7
#endif

#define MENU_AREA_OFFSET_Y (-10)

#define MENU_AREA_BORDER_HEIGHT 6
#define MENU_AREA_HEIGHT (MENU_AREA_BORDER_HEIGHT * 2 + MENU_LINE_HEIGHT * MENU_MAX_LINES)

// how opaque the bg is
#define MENU_OPACITY_MAX 20
// went the fg kicks in
#define MENU_OPACITY_THRESHOLD 6

// note this is assumed to be 16
#define MENU_ERROR_LEVEL_MAX 16
#define MENU_FLASH_LENGTH 32

#define menu_glypth_bitmap atlantis_glyph_bitmap
#define menu_glyph_widths atlantis_glyph_widths

struct text_element {
    const char *text;
    int16_t width;
    int16_t ellipsis_pos;
};

struct menu_state {
    struct mutex mutex;
    struct {
        struct text_element left_text;
        struct text_element right_text;
    } lines[MENU_MAX_LINES];
    bool disabled[MENU_MAX_LINES];
    int16_t opacity;
    int16_t current_height;
    bool appearing;
    bool disappearing;
    bool flashing;

    int8_t num_lines;
    int8_t selected_line;
    int8_t selection_top_pixel;
    int8_t flash_pos;
    int8_t error_level;

    // protected by mutex
    bool do_hide_menu;
    bool do_fill_menu;
};

extern uint8_t masked_run_aligned_cmd;
// buffer has 640 pixel run
int draw_menu_background(uint16_t *pixels, uint l, int line_length);

struct scanvideo_scanline_buffer;
void draw_menu_foreground(struct scanvideo_scanline_buffer *buffer);
void menu_cpu_blanking();
void menu_init();
void menu_display_blanking();

extern struct menu_state menu_state;
void set_disc_volume(int);
#ifndef NO_USE_CMD_LINE
void select_cmd_line_disc();
#endif

// called from CPU thread
void options_init();
bool handle_menu_key(int keycode, bool down);

#endif

enum option_cmd {
    OPTION_UPDATE,
    OPTION_EXECUTE,
    OPTION_CANCEL,
    OPTION_INIT_COMPLETE,
};

struct option;

typedef void (*option_handler)(struct option *option, enum option_cmd, int param);

struct option {
    option_handler handler;
    const char *txt;
    bool is_action;
    bool is_enter_option;
    int8_t value;
    int8_t old_value; // for use with change pending
    bool change_pending;
    const char *value_string;
    bool disabled;
};

void disc0_handler(struct option *option, enum option_cmd, int param);

#if X_GUI
extern bool xgui_paused;
#endif
#if PIXELATED_PAUSE
void set_pixelated_pause(bool pause);
#endif

#if defined(MODE_1080p) && defined(WIDESCREEN_OPTION)
void set_widescreen(bool widescreen);
void reflect_widescreen(bool widescreen);
#endif
void reflect_vpos_offset(int8_t value);
void reflect_frame_skip_count(int8_t value);
#ifdef __cplusplus
}
#endif

#endif
