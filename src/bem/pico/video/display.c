/* B-em v2.2 by Tom Walker
 * Pico version (C) 2021 Graham Sanderson
 */
#include <pico/time.h>
#include "pico.h"
#include "b-em.h"
#include "display.h"
#include "video_render.h"
#include "video.h"
#include "pico/sync.h"
#include "pico/multicore.h"

#if !X_GUI
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#else

#include "x_gui.h"

#endif

#include "menu.h"
#include "pico/binary_info.h"

//#define HACK_MODE7_FIELD 0

#if defined(MODE_1280) && !defined(X_GUI)
// draw a border briefly (initially) to give monitor a clue about the edges of the screen
#define ENABLE_BORDER
#endif

#if !X_GUI && !PICO_SCANVIDEO_LINKED_SCANLINE_BUFFERS
#error requires linked scanline buffers
#endif
static bool non_interlaced_teletext;
#define teletext_non_interlaced_teletext 1 // vestigial ... in teletext mode, we are now always non interlaced

#if 0
#define cmd_trace(...) printf(__VA_ARGS__)
#else
#define cmd_trace(...) ((void)0)
#endif

static bool debuggo = false;
static int8_t vpos_offset, next_vpos_offset;

void set_vpos_offset(int o) {
    next_vpos_offset = o;
}

#ifdef ENABLE_BORDER
uint8_t border_counter = 25;
#endif

#if PICO_ON_DEVICE
#include "menu.pio.h"
#include <hardware/structs/xip_ctrl.h>
#undef ENABLE_FRAME_SKIP
#else
#ifdef ENABLE_FRAME_SKIP
#ifndef DEFAULT_FRAME_SKIP_COUNT
#define DEFAULT_FRAME_SKIP_COUNT 0
#endif

int8_t next_frame_skip_count = DEFAULT_FRAME_SKIP_COUNT;
void set_frame_skip_count(int c) {
    if (c >= 0)
        next_frame_skip_count = c;
}

int frame_skip_count;
static int skip_counter;
static bool skip_frame;
#endif
#endif

// todo assertion failure (run out of space) - we should silently abort.
// todo discard characters when screen is shifted left
// todo add characters when screen is shifted right
// todo scry not used any more really?
// todo nula? shift possible - don't much care about attribute modes atm

static enum vid_disptype vid_dtype_intern;

#include "hardware/gpio.h"

CU_REGISTER_DEBUG_PINS(teletext, graphics, interlace, tiles, core_use, scanline, sync)
//CU_SELECT_DEBUG_PINS(sync)
//CU_SELECT_DEBUG_PINS(tiles)
//CU_SELECT_DEBUG_PINS(core_use)
//CU_SELECT_DEBUG_PINS(scanline)

//CU_SELECT_DEBUG_PINS(teletext)
//CU_SELECT_DEBUG_PINS(graphics)

// todo being wrong frame isn't helping
#if !defined(MODE_1280) && !defined(MODE_1080p)
#define HALF_LINE_HEIGHT 1
#else
#if PICO_ON_DEVICE
#define HALF_LINE_HEIGHT 2
#else
#define HALF_LINE_HEIGHT 1
#endif
#endif

bool force_tv_comma_1 = 1;

#if DISPLAY_MENU
uint8_t masked_run_aligned_cmd;
#endif

#ifdef DISPLAY_WIRE
#define static_if_display_wire static
#else
#define static_if_display_wire
#endif

static struct {
    int xpos;
    int end_xpos;
    uint16_t crtc_frame;
    int8_t hpos;
    int16_t lines_since_vsync_pos;
    int16_t first_display_enabled_line; // also since vsync pos;
    uint8_t sc;
    int8_t cdraw;
    bool cursor_on;
    bool had_half_line; // we have seen a half line since vsync pos
    bool half_line_scanline_added;
    uint32_t half_line_scanline_id;
    bool half_line_done;
    bool had_half_line_last;
    bool vdispen;
    bool interlace_field;
    bool vadj;
//    int firsty, lasty;
} pos_tracking; // this should all be legacy

static struct {
    uint8_t ula_ctrl; // todo split into fields
    uint8_t ula_mode;
    enum {
        TELETEXT = 0, HIGH_FREQ = 1, LOW_FREQ = 2
    } crtc_mode;
    uint8_t crtc_htotal, crtc_hsync_pos, crtc_hsync_width;
    // todo decode and remove
    uint8_t crtc8;

    int8_t top_of_screen_line;
} state;

static struct {
    const uint8_t *p[2];
    const uint8_t *heldp[2];
    const uint16_t *pixels_for_fg_bg;
    uint8_t buf[2];
    int8_t sc;
    bool need_new_lookup;
    int8_t col, bg; // todo col was initialized to 7 but imagine is anyway otherwise do in reset
    bool sep;
    bool dbl, nextdbl, wasdbl;
    bool gfx;
    bool flash, flashon;
    int8_t flashtime;
    uint8_t heldchar, holdchar;
} mode7;


static int char_width_pixels; // todo update

static struct scanvideo_scanline_buffer *scanline_buffer;
static uint16_t *current_scanline_pixels;
static uint16_t *current_scanline_pixels2;
static uint current_scanline_pixel_width;
static const uint8_t cursorlook[7] = {0, 0, 0, 0x80, 0x40, 0x20, 0x20};

static struct ALLEGRO_DISPLAY {
} _display;

static uint16_t ula_pal[16];         // maps from actual physical colour to bitmap display
static uint8_t ula_palbak[16];         // palette RAM in orginal ULA maps actual colour to logical colour
static uint16_t nula_collook[16];           // maps palette (logical) colours to 12-bit RGB

static int8_t nula_pal_write_flag = 0;
static uint8_t nula_pal_first_byte;
static uint8_t nula_flash[8];

static uint8_t nula_palette_mode;
#ifndef NO_USE_NULA_PIXEL_HSCROLL
static int nula_left_cut;
static int nula_left_edge;
#endif

static void (*current_displayed_chars_fn)(const uint8_t *dat, int count);

#if !X_GUI
struct scanvideo_mode video_mode;
extern const struct scanvideo_pio_program video_24mhz_composable;
#endif

#include "display_tables.h"

#if PIXELATED_PAUSE
static enum pixelated_pause_state display_pps;
static bool display_pps_unpause;

void pps_frame();
#ifndef BLANKING_FRAMES
#define BLANKING_FRAMES
#endif
#endif

#ifdef BLANKING_FRAMES
static int8_t blanking_frames;
#else
#define blanking_frames 0
#endif

// todo move to state
static int16_t vtotal_displayed, vtotal;

// Seems to be a win
#define USE_DIRTY_TILE_COUNT
static uint32_t tiles_dirty[8];
#ifdef USE_DIRTY_TILE_COUNT
static int dirty_tile_count;
#endif

#define PPS_VALID_LINE_COUNT 128
#if PIXELATED_PAUSE
uint32_t * const pps_work_area = (uint32_t *)mode7_pixels;
#if 0 // argh older gcc versions suck!
uint8_t * const pps_line_valid = (uint8_t *)(pps_work_area + 160);
uint8_t * const pps_mode7_alpha = (uint8_t *)(apps_line_valid + PPS_VALID_LINE_COUNT);
uint16_t * const pps_bitmap = (uint16_t *)(apps_mode7_alpha + 172); // 169 needed
#else
// although admittedly with this the compiler checks the bounds!
uint8_t * const pps_line_valid = (uint8_t *)(mode7_pixels[0][0] + 160 * 2);
uint8_t * const pps_mode7_alpha = (uint8_t *)(mode7_pixels[0][0] + 160 * 2 + PPS_VALID_LINE_COUNT / 2);
uint16_t * const pps_bitmap = (uint16_t *)(mode7_pixels[0][0] + 160 * 2 + PPS_VALID_LINE_COUNT / 2 + 172 / 2); // 169 needed
#endif
#define PP_SIZE (160 * 2 + PPS_VALID_LINE_COUNT / 2 + 172 / 2 + 80 * 256 / 2)
static_assert(PP_SIZE * 2 < sizeof(mode7_pixels), "");
uint16_t * pps_bitmap_line;
uint16_t pps_line_number;
// we only have RAM for 128 lines - but 1080p has 135!
#define check_pps_line_number(x) ((x) < 128)
#endif

static inline void inline_mode7_thing(struct aligned_8_pixels *dest, const struct maybe_aligned_8_pixels *src1,
                                      const struct maybe_aligned_8_pixels *src2) {
#if PICO_ON_DEVICE || PI_ASM32
    asm volatile (
    ".syntax unified\n"
    "lsrs r4, %1, #2 \n"
    "bcc 1f \n"
    "subs %1, #2 \n"
    "ldmia %1!, {r3, r4, r5, r6, r7} \n"
    "lsrs r3, #16 \n"
    "lsls %1, r4, #16 \n"
    "add r3, %1 \n"
    "lsrs r4, #16 \n"
    "lsls %1, r5, #16 \n"
    "add r4, %1 \n"
    "lsrs r5, #16 \n"
    "lsls %1, r6, #16 \n"
    "add r5, %1 \n"
    "lsrs r6, #16 \n"
    "lsls r7, #16 \n"
    "add r6, r7 \n"
    "stmia %0!, {r3, r4, r5, r6} \n"
    "lsrs r4, %2, #2 \n"
    "bcc 3f \n"
    "2: \n"
    "subs %2, #2 \n"
    "ldmia %2!, {r3, r4, r5, r6, r7} \n"
    "lsrs r3, #16 \n"
    "lsls %2, r4, #16 \n"
    "add r3, %2 \n"
    "lsrs r4, #16 \n"
    "lsls %2, r5, #16 \n"
    "add r4, %2 \n"
    "lsrs r5, #16 \n"
    "lsls %2, r6, #16 \n"
    "add r5, %2 \n"
    "lsrs r6, #16 \n"
    "lsls r7, #16 \n"
    "add r6, r7 \n"
    "stmia %0!, {r3, r4, r5, r6} \n"
    "b 4f\n"
    "1: \n"
    "ldmia %1!, {r4, r5, r6, r7} \n"
    "stmia %0!, {r4, r5, r6, r7} \n"
    "lsrs r4, %2, #2 \n"
    "bcs 2b\n"
    "3: \n"
    "ldmia %2!, {r4, r5, r6, r7} \n"
    "stmia %0!, {r4, r5, r6, r7} \n"
    "4:"
    : "+l" (dest), "+l" (src1), "+l" (src2)
    :
    : "r3", "r4", "r5", "r6", "r7", "memory", "cc"
    );
#else
    dest[0].p[0] = src1->p[0];
    dest[0].p[1] = src1->p[1];
    dest[0].p[2] = src1->p[2];
    dest[0].p[3] = src1->p[3];
    dest[0].p[4] = src1->p[4];
    dest[0].p[5] = src1->p[5];
    dest[0].p[6] = src1->p[6];
    dest[0].p[7] = src1->p[7];
    dest[1].p[0] = src2->p[0];
    dest[1].p[1] = src2->p[1];
    dest[1].p[2] = src2->p[2];
    dest[1].p[3] = src2->p[3];
    dest[1].p[4] = src2->p[4];
    dest[1].p[5] = src2->p[5];
    dest[1].p[6] = src2->p[6];
    dest[1].p[7] = src2->p[7];
#endif
}

#if PIXELATED_PAUSE
#define PP_MASK_1 PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0, 0x1f)
#define PP_MASK_2 PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0x1f, 0)
#endif

#define black_pixels8 ((struct maybe_aligned_8_pixels *)&mode7_chars[0])
#define black_aligned_8_pixels ((struct aligned_8_pixels *)&mode7_chars[0])
#define zeros16 ((const uint8_t *)&mode7_chars[0])

static inline uint32_t makecol(uint red, uint green, uint blue) {
    return PICO_SCANVIDEO_PIXEL_FROM_RGB8(red, green, blue);
}

static inline int get_freq_factor() {
    //return f = state.ula_mode + (state.ula_ctrl & 0x10 ? 0u : 1u);
    return state.ula_mode + state.crtc_mode - 1;
}

static void invalidate_tiles_for_palette_index(uint idx) {
//    printf("Update idx %x = %x\n", idx, color);
    int f = get_freq_factor();
    DEBUG_PINS_SET(tiles, 1);
    for (uint i = 0; i < 8; i++) {
//        printf("  %08x\n", palette_touch[f * 128 + idx * 8 + i]);
        uint32_t mask = palette_touch[f * 128 + idx * 8 + i];
#ifdef USE_DIRTY_TILE_COUNT
        uint32_t delta = mask & ~tiles_dirty[i];
        dirty_tile_count += __builtin_popcount(delta);
#endif
        tiles_dirty[i] |= mask;
    }
    DEBUG_PINS_CLR(tiles, 1);
}

static inline bool is_tile_dirty(uint8_t v) {
    // todo interpolator?
    return 1u & (tiles_dirty[v >> 5u] >> (v & 31u));
}

#ifndef NO_USE_NULA_PIXEL_HSCROLL
uint8_t nula_horizontal_offset;
uint8_t nula_left_blank;
#endif
uint8_t nula_disable;
#ifndef NO_USE_NULA_ATTRIBUTE
uint8_t nula_attribute_mode;
uint8_t nula_attribute_text;
#endif

static void update_tile_entry(uint idx) {
#ifdef USE_DIRTY_TILE_COUNT
    dirty_tile_count--;
#endif
    uint f = get_freq_factor();
    uint16_t *p = tile8[idx].p;
    DEBUG_PINS_SET(tiles, 4);
    tiles_dirty[idx >> 5u] &= ~(1u << (idx & 31u));
    //printf("Update tile entry %02x %d %x %x\n", idx, f, aa2f[idx], aa2f[(uint8_t)(idx<<1)]);
    uint16_t *palette = nula_palette_mode ? nula_collook : ula_pal;
    switch (f) {
        case 4:
            for (int j = 0; j < 8; j++) {
                p[j] = palette[aa2f[(uint8_t) idx]];
                idx <<= 2u;
                idx |= 3u;
            }
            break;
        case 3:
            for (int j = 0; j < 8; j++) {
                p[j] = palette[aa2f[(uint8_t) idx]];
                idx <<= 1u;
                idx |= 1u;
            }
            break;
        case 2:
            for (int j = 0; j < 4; j++) {
                p[0] = p[1] = palette[aa2f[(uint8_t) idx]];
                p += 2;
                idx <<= 1u;
                idx |= 1u;
            }
            break;
        case 1:
            // 4bpp
            for (int j = 0; j < 2; j++) {
                p[0] = p[1] = p[2] = p[3] = palette[aa2f[(uint8_t) idx]];
                p += 4;
                idx <<= 1u;
            }
            break;
        case 0:
            // 8bpp
            p[0] = p[1] = p[2] = p[3] = p[4] = p[5] = p[6] = p[7] = palette[aa2f[(uint8_t) idx]];
            break;
    }
    DEBUG_PINS_CLR(tiles, 4);
}

#if PIXELATED_PAUSE
static void update_tile_entry_pp(uint idx) {
    update_tile_entry(idx);
    uint16_t *p = tile8[idx].p;
    uint a1 = 0, a2 = 0;
    for(uint i=0;i<4;i++) {
        a1 += p[i] & PP_MASK_1;
        a2 += p[i] & PP_MASK_2;
    }
    uint b1 = 0, b2 = 0;
    for(uint i=4;i<8;i++) {
        b1 += p[i] & PP_MASK_1;
        b2 += p[i] & PP_MASK_2;
    }
    p[0] = ((a1 + b1) >> 3u) & PP_MASK_1;
    p[1] = ((a2 + b2) >> 3u) & PP_MASK_2;
    p[2] = (a1 >> 2u) & PP_MASK_1;
    p[3] = (a2 >> 2u) & PP_MASK_2;
    p[4] = (b1 >> 2u) & PP_MASK_1;
    p[5] = (b2 >> 2u) & PP_MASK_2;
}
#endif

#ifndef NO_USE_SAVE_STATE
void videoula_savestate(FILE * f)
{
    int c;
    uint32_t v;

    putc(state.ula_ctrl, f);
    for (c = 0; c < 16; c++)
        putc(ula_palbak[c], f);
    for (c = 0; c < 16; c++) {
        v = nula_collook[c];
        putc(((v >> 16) & 0xff), f); // red
        putc(((v >> 8) & 0xff), f);  // green
        putc((v & 0xff), f);         // blue
        putc(((v >> 24) & 0xff), f); // alpha
    }
    putc(nula_pal_write_flag, f);
    putc(nula_pal_first_byte, f);
    for (c = 0; c < 8; c++)
        putc(nula_flash[c], f);
    putc(nula_palette_mode, f);
    putc(nula_horizontal_offset, f);
    putc(nula_left_blank, f);
    putc(nula_disable, f);
    putc(nula_attribute_mode, f);
    putc(nula_attribute_text, f);
}

void videoula_loadstate(FILE * f)
{
    int c;
    uint8_t red, grn, blu, alp;
    videoula_write(0, getc(f));
    for (c = 0; c < 16; c++)
        videoula_write(1, getc(f) | (c << 4));
    for (c = 0; c < 16; c++) {
        red = getc(f);
        blu = getc(f);
        grn = getc(f);
        alp = getc(f);
        nula_collook[c] = (alp << 24) | (red << 16) | (grn << 8) | blu;
    }
    nula_pal_write_flag = getc(f);
    nula_pal_first_byte = getc(f);
    for (c = 0; c < 8; c++)
        nula_flash[c] = getc(f);
    nula_palette_mode = getc(f);
    nula_horizontal_offset = getc(f);
    nula_left_blank = getc(f);
    nula_disable = getc(f);
    nula_attribute_mode = getc(f);
    nula_attribute_text = getc(f);
}
#endif

static int warble;

const struct aligned_8_pixels *gen_cursor_tile8(int offset) {
    static struct aligned_8_pixels cursor8[2];
    static uint8_t which; // we need two for mode 7
    struct aligned_8_pixels *c = cursor8 + which;
    which ^= 1;
    uint16_t *p = c->p;
    if (current_scanline_pixels) {
        int x = pos_tracking.xpos * char_width_pixels + offset;
        if (x >= 0 && x < current_scanline_pixel_width - char_width_pixels) {
            if (state.crtc_mode != LOW_FREQ) {
                uint32_t *p32 = (uint32_t *) p;
                uint32_t *t32 = (uint32_t *) (current_scanline_pixels + x);
                uint32_t invert = PICO_SCANVIDEO_PIXEL_FROM_RGB8(255, warble ? 0 : 255, 255);
                invert |= invert << 16u;
                p32[0] = t32[0] ^ invert;
                p32[1] = t32[1] ^ invert;
                p32[2] = t32[2] ^ invert;
                p32[3] = t32[3] ^ invert;
            } else {
                uint16_t invert = PICO_SCANVIDEO_PIXEL_FROM_RGB8(255, 255, 255);
                uint16_t *t = current_scanline_pixels + x;
                p[0] = t[0] ^ invert;
                p[1] = t[2] ^ invert;
                p[2] = t[4] ^ invert;
                p[3] = t[6] ^ invert;
                p[4] = t[8] ^ invert;
                p[5] = t[10] ^ invert;
                p[6] = t[12] ^ invert;
                p[7] = t[14] ^ invert;
            }
        }
    }
    return c;
}

static inline void draw_tile8_hi(const struct aligned_8_pixels *tile) {
    // note xpos is signed and may be negative
    uint x = (uint) (pos_tracking.xpos * 8);
    if (x <= current_scanline_pixel_width - 8) {
        *(struct aligned_8_pixels *) (current_scanline_pixels + x) = *tile;
    }
    pos_tracking.xpos++;
}

static inline void draw_tile8_lo(const struct aligned_8_pixels *tile) {
    // note xpos is signed and may be negative
    uint x = (uint) (pos_tracking.xpos * 16);
    if (x <= current_scanline_pixel_width - 16) {
        struct aligned_8_pixels *pix = (struct aligned_8_pixels *) (current_scanline_pixels + x);
        pix[0].p[0] = tile->p[0];
        pix[0].p[1] = tile->p[0];
        pix[0].p[2] = tile->p[1];
        pix[0].p[3] = tile->p[1];
        pix[0].p[4] = tile->p[2];
        pix[0].p[5] = tile->p[2];
        pix[0].p[6] = tile->p[3];
        pix[0].p[7] = tile->p[3];
        pix[1].p[0] = tile->p[4];
        pix[1].p[1] = tile->p[4];
        pix[1].p[2] = tile->p[5];
        pix[1].p[3] = tile->p[5];
        pix[1].p[4] = tile->p[6];
        pix[1].p[5] = tile->p[6];
        pix[1].p[6] = tile->p[7];
        pix[1].p[7] = tile->p[7];
    }
    pos_tracking.xpos++;
}

extern void draw_pixels8x2(struct aligned_8_pixels *dest, const struct maybe_aligned_8_pixels *tile,
                           const struct maybe_aligned_8_pixels *tile2);
extern void draw_pixels8x2x2(struct aligned_8_pixels *dest, const struct maybe_aligned_8_pixels *tile,
                             const struct maybe_aligned_8_pixels *tile2,
                             struct aligned_8_pixels *dest2, const struct maybe_aligned_8_pixels *tile3,
                             const struct maybe_aligned_8_pixels *tile4);

static inline void __not_in_flash_func(draw_tiles8x2)(const struct maybe_aligned_8_pixels *tile,
                                                                const struct maybe_aligned_8_pixels *tile2) {
    int x = pos_tracking.xpos * char_width_pixels;
    assert(state.crtc_mode == TELETEXT); // only used for this
    // todo I wish I understood the teletxt delay!
    if (x >= 32) {
        x -= 32;
        assert(!(x & 7));
        if (current_scanline_pixels && x <= current_scanline_pixel_width - 16) {
            DEBUG_PINS_SET(teletext, 2);
            struct aligned_8_pixels *pix = (struct aligned_8_pixels *) (current_scanline_pixels + x);
#if PICO_ON_DEVICE || PI_ASM32
            draw_pixels8x2(pix, tile, tile2);
#else
            pix[0].p[0] = tile->p[0];
            pix[0].p[1] = tile->p[1];
            pix[0].p[2] = tile->p[2];
            pix[0].p[3] = tile->p[3];
            pix[0].p[4] = tile->p[4];
            pix[0].p[5] = tile->p[5];
            pix[0].p[6] = tile->p[6];
            pix[0].p[7] = tile->p[7];
            pix[1].p[0] = tile2->p[0];
            pix[1].p[1] = tile2->p[1];
            pix[1].p[2] = tile2->p[2];
            pix[1].p[3] = tile2->p[3];
            pix[1].p[4] = tile2->p[4];
            pix[1].p[5] = tile2->p[5];
            pix[1].p[6] = tile2->p[6];
            pix[1].p[7] = tile2->p[7];
#endif
            DEBUG_PINS_CLR(teletext, 2);
        }
    }
}

static inline void __not_in_flash_func(draw_tiles8x2x2_aligned)(
        const struct aligned_8_pixels *tile, const struct aligned_8_pixels *tile2,
        const struct aligned_8_pixels *tile3, const struct aligned_8_pixels *tile4) {
    int x = pos_tracking.xpos * char_width_pixels;
    assert(state.crtc_mode == TELETEXT); // only used for this
    // todo I wish I understood the teletxt delay!
    x -= 32;
    if (x >= 0) {
        assert(!(x & 7));
        if (current_scanline_pixels && x <= current_scanline_pixel_width - 16) {
            struct aligned_8_pixels *pix = (struct aligned_8_pixels *) (current_scanline_pixels + x);
            DEBUG_PINS_SET(teletext, 2);
            pix[0] = *tile;
            pix[1] = *tile2;
            if (current_scanline_pixels2) {
                pix = (struct aligned_8_pixels *) (current_scanline_pixels2 + x);
                pix[0] = *tile3;
                pix[1] = *tile4;
            }
            DEBUG_PINS_CLR(teletext, 2);
        }
    }
    pos_tracking.xpos++;
}

static void set_all_tiles_dirty() {
    memset(tiles_dirty, 0xff, 32);
#ifdef USE_DIRTY_TILE_COUNT
    dirty_tile_count = 256;
#endif
}

void nula_default_palette(void) {
    for (int i = 0; i < 16; i++) {
        nula_collook[i] = PICO_SCANVIDEO_PIXEL_FROM_RGB8((i & 1) * 0xff, ((i >> 1) & 1) * 0xff, ((i >> 2) & 1) * 0xff);
    }
    mode7.need_new_lookup = 1;
    set_all_tiles_dirty();
}

static __noinline void mode7_gen_nula_lookup(void) {
    int fg_ix, fg_pix, fg_red, fg_grn, fg_blu;
    int bg_ix, bg_pix, bg_red, bg_grn, bg_blu;
    int lu_red, lu_grn, lu_blu;

#if PIXELATED_PAUSE
    if (display_pps) {
        set_all_tiles_dirty(); // we are storing color values in the tiles during display_pps
        mode7.need_new_lookup = 0; // and we have data in the mode7 chars!
        return;
    }
#endif

    for (fg_ix = 0; fg_ix < 8; fg_ix++) {
        fg_pix = nula_collook[fg_ix];
        fg_red = PICO_SCANVIDEO_R5_FROM_PIXEL(fg_pix);
        fg_grn = PICO_SCANVIDEO_G5_FROM_PIXEL(fg_pix);
        fg_blu = PICO_SCANVIDEO_B5_FROM_PIXEL(fg_pix);
        for (bg_ix = 0; bg_ix < 8; bg_ix++) {
            bg_pix = nula_collook[bg_ix];
            bg_red = PICO_SCANVIDEO_R5_FROM_PIXEL(bg_pix);
            bg_grn = PICO_SCANVIDEO_G5_FROM_PIXEL(bg_pix);
            bg_blu = PICO_SCANVIDEO_B5_FROM_PIXEL(bg_pix);
            // todo hopelessly inefficient
            for (int i = 0; i < count_of(grey_pixels); i++) {
                int weight = grey_pixels[i];
                lu_red = bg_red + ((((fg_red - bg_red) * weight) * 17) >> 8);
                lu_grn = bg_grn + ((((fg_grn - bg_grn) * weight) * 17) >> 8);
                lu_blu = bg_blu + ((((fg_blu - bg_blu) * weight) * 17) >> 8);
                mode7_pixels[fg_ix][bg_ix][i] = PICO_SCANVIDEO_PIXEL_FROM_RGB5(lu_red, lu_grn, lu_blu);
            }
        }
    }
    mode7.need_new_lookup = 0;
}

static inline void __time_critical_func(mode7_color_updated)() {
    mode7.pixels_for_fg_bg = mode7_pixels[mode7.col][mode7.bg];
}

static void __time_critical_func(draw_bytes_teletext)(const uint8_t *dat_ptr, int len) {
    int t;
    const uint8_t *mode7_px[2];

    if (mode7.need_new_lookup)
        mode7_gen_nula_lookup();

    // todo what is this for, is it related to delay, or what?
    if (pos_tracking.xpos + len >= (1280 - 32) / 16) {
        len = (1280 - 32) / 16 - pos_tracking.xpos;
    }
    for (int i = 0; i < len; i++) {
        DEBUG_PINS_SET(teletext, 4);

        t = mode7.buf[0];
        mode7.buf[0] = mode7.buf[1];
        mode7.buf[1] = dat_ptr ? (dat_ptr[i] & 0x7f) : 255;
        uint8_t dat = t;

        mode7_px[0] = mode7.p[0];
        mode7_px[1] = mode7.p[1];

        if (dat == 255) {
            if (!teletext_non_interlaced_teletext) {
                draw_tiles8x2(black_pixels8, black_pixels8);
                pos_tracking.xpos++;
            } else {
                draw_tiles8x2x2_aligned(black_aligned_8_pixels, black_aligned_8_pixels, black_aligned_8_pixels,
                                        black_aligned_8_pixels);
            }
            continue;
        }

        int holdoff = 0, holdclear = 0;
        int mode7_flashx = mode7.flash, mode7_dblx = mode7.dbl;

        if (dat < 0x20) {
            switch (dat) {
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                    mode7.gfx = 0;
                    mode7.col = dat&7;
                    mode7_color_updated();
                    mode7.p[0] = mode7_chars;
                    mode7.p[1] = mode7_charsi;
                    holdclear = 1;
                    break;
                case 8:
                    mode7.flash = 1;
                    break;
                case 9:
                    mode7.flash = 0;
                    break;
                case 12:
                case 13:
                    mode7.dbl = dat & 1;
                    if (mode7.dbl)
                        mode7.wasdbl = 1;
                    break;
                case 17:
                case 18:
                case 19:
                case 20:
                case 21:
                case 22:
                case 23:
                    mode7.gfx = 1;
                    mode7.col = dat & 7;
                    mode7_color_updated();
                    if (mode7.sep) {
                        mode7.p[0] = mode7_sepgraph;
                        mode7.p[1] = mode7_sepgraphi;
                    } else {
                        mode7.p[0] = mode7_graph;
                        mode7.p[1] = mode7_graphi;
                    }
                    break;
                case 24:
                    mode7.col = mode7.bg;
                    mode7_color_updated();
                    break;
                case 25:
                    if (mode7.gfx) {
                        mode7.p[0] = mode7_graph;
                        mode7.p[1] = mode7_graphi;
                    }
                    mode7.sep = 0;
                    break;
                case 26:
                    if (mode7.gfx) {
                        mode7.p[0] = mode7_sepgraph;
                        mode7.p[1] = mode7_sepgraphi;
                    }
                    mode7.sep = 1;
                    break;
                case 28:
                    mode7.bg = 0;
                    mode7_color_updated();
                    break;
                case 29:
                    mode7.bg = mode7.col;
                    mode7_color_updated();
                    break;
                case 30:
                    mode7.holdchar = 1;
                    break;
                case 31:
                    holdoff = 1;
                    break;
            }
            if (mode7.holdchar) {
                dat = mode7.heldchar;
                if (dat >= 0x40 && dat < 0x60)
                    dat = 32;
                mode7_px[0] = mode7.heldp[0];
                mode7_px[1] = mode7.heldp[1];
            } else
                dat = 0x20;
            if (mode7_dblx != mode7.dbl)
                dat = 32;           /*Double height doesn't respect held characters */
        } else if (mode7.p[0] != mode7_chars) {
            mode7.heldchar = dat;
            mode7.heldp[0] = mode7_px[0];
            mode7.heldp[1] = mode7_px[1];
        }

        //            DEBUG_PINS_CLR(teletext, 1);
        int x = pos_tracking.xpos * 16;
        // todo I wish I understood the teletxt delay!
        x -= 32;
        if (x >= 0) {
            assert(!(x & 7));
            if (current_scanline_pixels && x <= current_scanline_pixel_width - 16) {

                if (!teletext_non_interlaced_teletext) {
                    // t = 0..1920 i.e. row index in our lookup table for 2 span indexes (span being 8 pixels of grayscale)
                    if (mode7_dblx && !mode7.nextdbl)
                        t = ((dat - 0x20) * 20) + ((mode7.sc >> 1) * 2);
                    else if (mode7_dblx)
                        t = ((dat - 0x20) * 20) + ((mode7.sc >> 1) * 2) + (5 * 2);
                    else
                        t = ((dat - 0x20) * 20) + (mode7.sc * 2);
                    int intl = 0;
                    if ((mode7_flashx && !mode7.flashon) || (!mode7.dbl && mode7.nextdbl)) {
                        t = 0;
                    } else if (mode7_dblx) {
                        intl = pos_tracking.sc & 1;
                    } else {
#ifndef HACK_MODE7_FIELD
                        intl = vid_dtype_intern == VDT_INTERLACE && pos_tracking.interlace_field;
#else
                        intl = HACK_MODE7_FIELD;
#endif
                    }
                    const struct maybe_aligned_8_pixels *pixels = (const struct maybe_aligned_8_pixels *) (
                            mode7.pixels_for_fg_bg + mode7_px[intl][t]);
                    const struct maybe_aligned_8_pixels *pixels2 = (const struct maybe_aligned_8_pixels *) (
                            mode7.pixels_for_fg_bg + mode7_px[intl][t + 1]);

                    draw_tiles8x2(pixels, pixels2);
                } else {
                    //            DEBUG_PINS_SET(teletext, 1);
                    int intl0 = 0;
                    int intl1 = 1;
                    // todo collapse this logging
                    if ((mode7_flashx && !mode7.flashon) || (!mode7.dbl && mode7.nextdbl)) {
                        t = 0;
                    } else {
                        if (mode7_dblx) {
                            intl0 = intl1 = pos_tracking.sc & 1;
                        }
                        t = __fast_mul((dat - 0x20), 20);
                        if (mode7_dblx) {
                            t += ((mode7.sc >> 1) * 2);
                            if (mode7.nextdbl) {
                                t += 10;
                            }
                        } else {
                            t += (mode7.sc * 2);
                        }
                    }
                    struct aligned_8_pixels *pix = (struct aligned_8_pixels *) (current_scanline_pixels + x);
#if PIXELATED_PAUSE
                    if (display_pps) {
#ifdef USE_DIRTY_TILE_COUNT
                        assert(display_pps == PPS_SETUP_FRAME);
                        // todo cache this?
                        int tnum = mode7.col * 8 + mode7.bg;
                        tnum *= 4;
                        uint32_t *color_range = (uint32_t*)&tile8[tnum];
                        if (dirty_tile_count) {
                            if (is_tile_dirty(tnum)) {
                                dirty_tile_count--;
                                tiles_dirty[tnum >> 5u] &= ~(1u << (tnum & 31u));
                                int fg_pix = nula_collook[mode7.col & 7];
                                int fg_red = PICO_SCANVIDEO_R5_FROM_PIXEL(fg_pix);
                                int fg_blu = PICO_SCANVIDEO_B5_FROM_PIXEL(fg_pix);
                                int bg_pix = nula_collook[mode7.bg];
                                int bg_red = PICO_SCANVIDEO_R5_FROM_PIXEL(bg_pix);
                                int bg_blu = PICO_SCANVIDEO_B5_FROM_PIXEL(bg_pix);
                                // todo hopelessly inefficient
                                for (int weight = 0; weight < 16; weight++) {
                                    int lu_red = bg_red + ((((fg_red - bg_red) * weight) * 17) >> 8);
                                    int lu_blu = bg_blu + ((((fg_blu - bg_blu) * weight) * 17) >> 8);
                                    color_range[weight] = PICO_SCANVIDEO_PIXEL_FROM_RGB5(lu_red, 0, lu_blu);
                                }
                                for (int weight = 0; weight < 16; weight++) {
                                    int fg_grn = PICO_SCANVIDEO_G5_FROM_PIXEL(fg_pix);
                                    int bg_grn = PICO_SCANVIDEO_G5_FROM_PIXEL(bg_pix);
                                    int lu_grn = bg_grn + ((((fg_grn - bg_grn) * weight) * 17) >> 8);
                                    color_range[16+weight] = PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, lu_grn, 0);
                                }
                            }
                        }

                        // these are the four alpha values from 0-15
                        // p0, p1 (left, right 8 pixels of top half row)
                        // p2, p3 (left, right 8 pixels of bottom half row)
                        int a0 = pps_mode7_alpha[mode7_px[intl0][t]];
                        int a1 = pps_mode7_alpha[mode7_px[intl0][t+1]];
                        int a2 = pps_mode7_alpha[mode7_px[intl1][t]];
                        int a3 = pps_mode7_alpha[mode7_px[intl1][t+1]];
                        pps_work_area[(x/16)*4] += color_range[(a0 + a2)/2];
                        pps_work_area[(x/16)*4+1] += color_range[16+(a0 + a2)/2];
                        pps_work_area[(x/16)*4+2] += color_range[(a1 + a3)/2];
                        pps_work_area[(x/16)*4+3] += color_range[16+(a1 + a3)/2];
#else
#error expected USE_DIRTY_TILE_COUNT - never wrote a version of the above which didnt use it
#endif
                    } else
#endif
                    {
                        const struct maybe_aligned_8_pixels *pixels = (const struct maybe_aligned_8_pixels *) (
                                mode7.pixels_for_fg_bg + mode7_px[intl0][t]);
                        const struct maybe_aligned_8_pixels *pixels2 = (const struct maybe_aligned_8_pixels *) (
                                mode7.pixels_for_fg_bg + mode7_px[intl0][t + 1]);
                        const struct maybe_aligned_8_pixels *pixels3 = (const struct maybe_aligned_8_pixels *) (
                                mode7.pixels_for_fg_bg + mode7_px[intl1][t]);
                        const struct maybe_aligned_8_pixels *pixels4 = (const struct maybe_aligned_8_pixels *) (
                                mode7.pixels_for_fg_bg + mode7_px[intl1][t + 1]);

                        DEBUG_PINS_SET(teletext, 2);
                        // ok to draw nothing when this is not set, as we are only called when in non interlaced mode 7 mode,
                        // and this is only null during brief mode change races
                        if (current_scanline_pixels2) {
                            struct aligned_8_pixels *pix2 = (struct aligned_8_pixels *) (current_scanline_pixels2 + x);
//                        draw_pixels8x2x2(pix, pixels, pixels2, pix2, pixels3, pixels4);
                            inline_mode7_thing(pix, pixels, pixels2);
                            inline_mode7_thing(pix2, pixels3, pixels4);
                        }
                    }
                    DEBUG_PINS_CLR(teletext, 2);
                }
            } else {
                break;
            }
        }
        pos_tracking.xpos++;

        if (holdoff) {
            mode7.holdchar = 0;
            mode7.heldchar = 32;
        }
        if (holdclear)
            mode7.heldchar = 32;
        DEBUG_PINS_CLR(teletext, 4);
    }
}

static void __time_critical_func(draw_bytes_non_teletext_hi)(const uint8_t *dat, int len) {
#ifndef NO_USE_NULA_ATTRIBUTE
#error needs updating
    if (nula_attribute_mode && state.ula_mode > 1) {
        if (state.ula_mode == 3) {
            // 1bpp
            if (nula_attribute_text) {
                int attribute = ((dat & 7) << 1);
                float pc = 0.0f;
                for (c = 0; c < 7; c++, pc += 0.75f) {
                    int output = ula_pal[attribute | (dat >> (7 - (int) pc) & 1)];
                    nula_putpixel(pos_tracking.scrx + c, output);
                }
                // Very loose approximation of the text attribute mode
                nula_putpixel(pos_tracking.scrx + 7, ula_pal[attribute]);
            } else {
                int attribute = ((dat & 3) << 2);
                float pc = 0.0f;
                for (c = 0; c < 8; c++, pc += 0.75f) {
                    int output = ula_pal[attribute | (dat >> (7 - (int) pc) & 1)];
                    nula_putpixel(pos_tracking.scrx + c, output);
                }
            }
        } else {
            int attribute = (((dat & 16) >> 1) | ((dat & 1) << 2));
            fl?^oat pc = 0.0f;
            for (c = 0; c < 8; c++, pc += 0.75f) {
                int a = 3 - ((int) pc) / 2;
                int output = ula_pal[attribute | ((dat >> (a + 3)) & 2) | ((dat >> a) & 1)];
                nula_putpixel(pos_tracking.scrx + c, output);
            }
        }
    } else
#endif
    {
#ifdef USE_DIRTY_TILE_COUNT
#if PIXELATED_PAUSE
        if (display_pps) {
            assert(display_pps == PPS_SETUP_FRAME);
            if (dirty_tile_count) {
                for (int i = 0; i < len; i++) {
                    if (is_tile_dirty(dat[i])) {
                        update_tile_entry_pp(dat[i]);
                    }
                }
            }
            for (int i = 0; i < len; i++) {
                if (pos_tracking.xpos < 80) {
                    struct aligned_8_pixels *tile = tile8 + dat[i];
                    pps_work_area[pos_tracking.xpos*2] += tile->p[0];
                    pps_work_area[pos_tracking.xpos*2+1] += tile->p[1];
                }
                // todo we should just be able to do a pos_tracking.xpos++ here
                //  as the line is going to be black anyway, but i see visual glitching (at
                //  least while running a program that is constantly changing the palette
                //  so leaving it in for now, though it is a bit of a waste of time/space
                draw_tile8_hi(black_aligned_8_pixels);
            }
        } else
#endif
        {
            if (dirty_tile_count) {
                for (int i = 0; i < len; i++) {
                    if (is_tile_dirty(dat[i])) {
                        update_tile_entry(dat[i]);
                    }
                }
            }
            for (int i = 0; i < len; i++) {
                draw_tile8_hi(tile8 + dat[i]);
            }
        }
#else
        for (int i = 0; i < len; i++) {
            if (is_tile_dirty(dat[i])) {
                update_tile_entry(dat[i]);
            }
            draw_tile8_hi(tile8 + dat[i]);
        }
#endif
    }
}

static void __time_critical_func(draw_bytes_non_teletext_lo)(const uint8_t *dat, int len) {
#ifndef NO_USE_NULA_ATTRIBUTE
#error needs updating
    if (nula_attribute_mode && state.ula_mode > 1) {
        if (state.ula_mode == 3) {
            // 1bpp
            if (nula_attribute_text) {
                int attribute = ((dat & 7) << 1);
                float pc = 0.0f;
                for (c = 0; c < 7; c++, pc += 0.75f) {
                    int output = ula_pal[attribute | (dat >> (7 - (int) pc) & 1)];
                    nula_putpixel(pos_tracking.scrx + c, output);
                }
                // Very loose approximation of the text attribute mode
                nula_putpixel(pos_tracking.scrx + 7, ula_pal[attribute]);
            } else {
                int attribute = ((dat & 3) << 2);
                float pc = 0.0f;
                for (c = 0; c < 8; c++, pc += 0.75f) {
                    int output = ula_pal[attribute | (dat >> (7 - (int) pc) & 1)];
                    nula_putpixel(pos_tracking.scrx + c, output);
                }
            }
        } else {
            int attribute = (((dat & 16) >> 1) | ((dat & 1) << 2));
            float pc = 0.0f;
            for (c = 0; c < 8; c++, pc += 0.75f) {
                int a = 3 - ((int) pc) / 2;
                int output = ula_pal[attribute | ((dat >> (a + 3)) & 2) | ((dat >> a) & 1)];
                nula_putpixel(pos_tracking.scrx + c, output);
            }
        }
    } else
#endif
    {
#ifdef USE_DIRTY_TILE_COUNT
#if PIXELATED_PAUSE
        if (display_pps) {
            assert(display_pps == PPS_SETUP_FRAME);
            if (dirty_tile_count) {
                for (int i = 0; i < len; i++) {
                    if (is_tile_dirty(dat[i])) {
                        update_tile_entry_pp(dat[i]);
                    }
                }
            }
            for (int i = 0; i < len; i++) {
                if (pos_tracking.xpos < 40) {
                    struct aligned_8_pixels *tile = tile8 + dat[i];
                    pps_work_area[pos_tracking.xpos*4] += tile->p[2];
                    pps_work_area[pos_tracking.xpos*4+1] += tile->p[3];
                    pps_work_area[pos_tracking.xpos*4+2] += tile->p[4];
                    pps_work_area[pos_tracking.xpos*4+3] += tile->p[5];
                }
                // todo we should just be able to do a pos_tracking.xpos++ here
                //  as the line is going to be black anyway, but i see visual glitching (at
                //  least while running a program that is constantly changing the palette
                //  so leaving it in for now, though it is a bit of a waste of time/space
                draw_tile8_lo(black_aligned_8_pixels);
            }
        } else
#endif
        {
            if (dirty_tile_count) {
                for (int i = 0; i < len; i++) {
                    if (is_tile_dirty(dat[i])) {
                        update_tile_entry(dat[i]);
                    }
                }
            }
            for (int i = 0; i < len; i++) {
                draw_tile8_lo(tile8 + dat[i]);
            }
        }
#else
        for (int i = 0; i < len; i++) {
            if (is_tile_dirty(dat[i])) {
                update_tile_entry(dat[i]);
            }
            draw_tile8_lo(tile8 + dat[i]);
        }
#endif
    }
}

static void __not_in_flash_func(draw_bytes_black_hi)(__unused const uint8_t *dat, int len) {
    // Gaps between lines in modes 3 & 6.
    for (int i = 0; i < len; i++) {
        draw_tile8_hi(black_aligned_8_pixels);
    }
}

static void __not_in_flash_func(draw_bytes_black_lo)(const uint8_t *dat, int len) {
    int tmp = pos_tracking.xpos;
    pos_tracking.xpos *= 2;
    draw_bytes_black_hi(dat, len * 2);
    pos_tracking.xpos = tmp + len;
}

static uint8_t default_hpos[3] = {
        16, // TELETEXT
        37, // HIGH FREQ
        18, // LOW FREQ
};

static void update_hpos() {
    int hpos = state.crtc_htotal - default_hpos[state.crtc_mode];
    hpos -= state.crtc_hsync_pos;
    hpos += state.crtc_hsync_width;
    pos_tracking.hpos = hpos;
}

static void update_displayed_char_fn() {
    if ((state.crtc8 & 0x30u) == 0x30u || ((pos_tracking.sc & 0x08u) && state.crtc_mode != TELETEXT)) {
        current_displayed_chars_fn = state.crtc_mode == HIGH_FREQ ? draw_bytes_black_hi : draw_bytes_black_lo;
    } else {
        switch (state.crtc_mode) {
            case TELETEXT:
                current_displayed_chars_fn = draw_bytes_teletext;
                break;
            case HIGH_FREQ:
                current_displayed_chars_fn = draw_bytes_non_teletext_hi;
                break;
            default:
                current_displayed_chars_fn = draw_bytes_non_teletext_lo;
                break;
        }
    }
}

void nula_reset() {
    // Reset NULA
    nula_palette_mode = 0;
#ifndef NO_USE_NULA_PIXEL_HSCROLL
    nula_horizontal_offset = 0;
                    nula_left_blank = 0;
#endif
#ifndef NO_USE_NULA_ATTRIBUTE
    nula_attribute_mode = 0;
                    nula_attribute_text = 0;
#endif
    // Reset palette
    nula_default_palette();

    // Reset flash
    for (uint c = 0; c < 8; c++) {
        nula_flash[c] = 1;
    }
}

static_if_display_wire void __time_critical_func(effect_ula_write)(uint addr, uint val) {
    int c;
    if (nula_disable)
        addr &= ~2u;             // nuke additional NULA addresses

    switch (addr & 3u) {
        case 0: {
            // Video control register.
            // log_debug("video: ULA write VCR from %04X: %02X %i %i\n",pc,val,hc,vc);

            if ((state.ula_ctrl ^ val) & 1u) {
                // Flashing colour control bit has changed.
                if (val & 1u) {
                    for (c = 0; c < 16; c++) {
                        if ((ula_palbak[c] & 8) && nula_flash[(ula_palbak[c] & 7u) ^ 7u])
                            ula_pal[c] = nula_collook[ula_palbak[c] & 15u];
                        else
                            ula_pal[c] = nula_collook[(ula_palbak[c] & 15u) ^ 7u];
                    }
                } else {
                    for (c = 0; c < 16; c++)
                        ula_pal[c] = nula_collook[(ula_palbak[c] & 15u) ^ 7u];
                }
                for (c = 0; c < 16; c++) {
                    if ((ula_palbak[c] & 8u) && nula_flash[(ula_palbak[c] & 7u) ^ 7u])
                        invalidate_tiles_for_palette_index(c);
                }
            }
//            if ((val ^ state.ula_ctrl) & 0x10u) {
//                printf("High/lo changed to %d\n", !!(val & 0x10u));
//            }
            state.ula_ctrl = val;
            int old_ula_mode = state.ula_mode;
            state.ula_mode = (state.ula_ctrl >> 2u) & 3u;
            char_width_pixels = ((state.ula_ctrl & 0x10u) ? 8 : 16);
            if (state.ula_mode != old_ula_mode) {
                set_all_tiles_dirty();
            }
            if (val & 2u)
                state.crtc_mode = TELETEXT;
            else if (val & 0x10u)
                state.crtc_mode = HIGH_FREQ;
            else
                state.crtc_mode = LOW_FREQ;
            non_interlaced_teletext = state.crtc_mode == TELETEXT;
            update_hpos();
            update_displayed_char_fn();
//            static char *state.crtc_mode_s[] = { "teletext", "high-freq", "low_freq" };
//            printf("Ula configure %s cols=%d freq=%d\n", state.crtc_mode_s[state.crtc_mode], (1<<state.ula_mode)*10, 2<<state.ula_mode);
            set_intern_dtype(vid_dtype_user);
        }
            break;

        case 1: {
            // Palette register.
            // log_debug("video: ULA write palette from %04X: %02X map l=%x->p=%x %i %i\n",pc,val, val >> 4, (val & 0x0f) ^ 0x07, hc, vc);
            uint8_t code = val >> 4;
            ula_palbak[code] = val & 15;
            ula_pal[code] = nula_collook[(val & 15) ^ 7];
            if ((val & 8) && (state.ula_ctrl & 1) && nula_flash[val & 7])
                ula_pal[code] = nula_collook[val & 15];
//            printf("Updated pal %d to (NOT) %d: %04x\n", code,7^(val & 15), ula_pal[code]);
            invalidate_tiles_for_palette_index(code);
        }
            break;

        case 2:                     // &FE22 = NULA CONTROL REG
        {
            uint8_t code = val >> 4;
            uint8_t param = val & 0xf;

            switch (code) {
                case 1:
                    nula_palette_mode = param & 1;
                    break;

#ifndef NO_USE_NULA_ATTRIBUTE
                    case 2:
                        nula_horizontal_offset = param & 7;
                        break;

                    case 3:
                        nula_left_blank = param & 15;
                        break;
#endif
                case 4:
                    nula_reset();
                    break;

                case 5:
                    nula_disable = 1;
                    break;

#ifndef NO_USE_NULA_ATTRIBUTE
                    case 6:
                        nula_attribute_mode = param & 1;
                        break;

                    case 7:
                        nula_attribute_text = param & 1;
                        break;
#endif
                case 8:
                    nula_flash[0] = param & 8;
                    nula_flash[1] = param & 4;
                    nula_flash[2] = param & 2;
                    nula_flash[3] = param & 1;
                    break;

                case 9:
                    nula_flash[4] = param & 8;
                    nula_flash[5] = param & 4;
                    nula_flash[6] = param & 2;
                    nula_flash[7] = param & 1;
                    break;

                default:
                    break;
            }

        }
            break;

        case 3:                     // &FE23 = NULA PALETTE REG
        {
            if (nula_pal_write_flag) {
                // Commit the write to palette
                int c = (nula_pal_first_byte >> 4);
                int r = nula_pal_first_byte & 0x0f;
                int g = (val & 0xf0) >> 4;
                int b = val & 0x0f;
                nula_collook[c] = makecol(r | r << 4, g | g << 4, b | b << 4);
                // Manual states colours 8-15 are set solid by default
                if (c & 8)
                    nula_flash[c - 8] = 0;
                // Reset all colour lookups
                for (c = 0; c < 16; c++) {
                    ula_pal[c] = nula_collook[(ula_palbak[c] & 15) ^ 7];
                    if ((ula_palbak[c] & 8) && (state.ula_ctrl & 1) && nula_flash[(ula_palbak[c] & 7) ^ 7])
                        ula_pal[c] = nula_collook[ula_palbak[c] & 15];
                }
                set_all_tiles_dirty();
                mode7.need_new_lookup = 1;
            } else {
                // Remember the first byte
                nula_pal_first_byte = val;
            }

            nula_pal_write_flag = !nula_pal_write_flag;
        }
            break;

    }
}

int avg_color(int col, int col2) {
    int r = (PICO_SCANVIDEO_R5_FROM_PIXEL(col) + PICO_SCANVIDEO_R5_FROM_PIXEL(col2)) / 2;
    int g = (PICO_SCANVIDEO_G5_FROM_PIXEL(col) + PICO_SCANVIDEO_G5_FROM_PIXEL(col2)) / 2;
    int b = (PICO_SCANVIDEO_B5_FROM_PIXEL(col) + PICO_SCANVIDEO_B5_FROM_PIXEL(col2)) / 2;
    return PICO_SCANVIDEO_PIXEL_FROM_RGB5(r, g, b);
}

#if !X_GUI
#if !defined(MODE_1280) && !defined(MODE_1080p)
#if PICO_ON_DEVICE
#define TIMING_MULT 6
#else
#define TIMING_MULT 1
#endif

// this is a hack to override the actual clock freq
const uint32_t video_clock_freq = 24000000 * TIMING_MULT;

const struct scanvideo_timing vga_timing_50_default =
        {
                .clock_freq = 19600000,

                .h_active = 640,
                .v_active = 480,

                .h_front_porch = 1 * 8,
                .h_pulse = 8 * 8,
                .h_total = 98 * 8,
                .h_sync_polarity = 1,

                .v_front_porch = 1,
                .v_pulse = 3,
                .v_total = 500,
                .v_sync_polarity = 1,

                .enable_clock = 0,
                .clock_polarity = 0,

                .enable_den = 0
        };


const struct scanvideo_mode vga_mode_640x256_50_bem =
        {
                .default_timing = &vga_timing_50_default,
                .pio_program = &video_24mhz_composable,
                .width = 640,
                .height = 256,
                .xscale = 1,
                .yscale = 1,
        };

#define vga_mode vga_mode_640x256_50_bem

#elif defined(MODE_1280)
// todo bi_decl should be available anyway
#if !PICO_NO_BINARY_INFO
bi_decl(bi_program_feature("1280x1024 50Hz video mode (non standard)"));
#endif

// note this is 1280x1024, but we don't have square pixels in that mode at 270Mhz
const struct scanvideo_timing vga_timing_640x1024_50 =
        {
                .clock_freq = 45000000,

                .h_active = 640,
#if PICO_ON_DEVICE
                .v_active = 1024,
#else
                .v_active = 512,
#endif

                .h_front_porch = 9 * 8 / 2,
                .h_pulse = 17 * 8 / 2,
                .h_total = 212 * 8 / 2,
                .h_sync_polarity = 0,

                .v_front_porch = 1,
                .v_pulse = 2,
                .v_total = 1061,
                .v_sync_polarity = 0,

                .enable_clock = 0,
                .clock_polarity = 0,

                .enable_den = 0
        };

const struct scanvideo_mode vga_mode_640x256_50_bem_1280 =
        {
                .default_timing = &vga_timing_640x1024_50,
                .pio_program = &video_24mhz_composable,
                .width = 640,
                .height = 256,
                .xscale = 1,
                .yscale = 1,
        };

#define vga_mode vga_mode_640x256_50_bem_1280

#elif defined(MODE_1080p)
// todo bi_decl should be available anyway
#if !PICO_NO_BINARY_INFO
bi_decl(bi_program_feature("1080p 50Hz video mode"));
#endif

const struct scanvideo_timing vga_timing_1920x1080_50 =
        {
                .clock_freq = 148500000,

                .h_active = 1920,
#if PICO_ON_DEVICE
                .v_active = 1080,
#else
                .v_active = 540,
#endif

                .h_front_porch = 528,
                .h_pulse = 44,
                .h_total = 2640,
                .h_sync_polarity = 1,

                .v_front_porch = 4,
                .v_pulse = 5,
                .v_total = 1125,
                .v_sync_polarity = 1,

                .enable_clock = 0,
                .clock_polarity = 0,

                .enable_den = 0
        };

const struct scanvideo_mode vga_mode_640x270_50_bem_1080p =
        {
                .default_timing = &vga_timing_1920x1080_50,
                .pio_program = &video_24mhz_composable,
                .width = 640,
                .height = 270,
                .xscale = 3,
                .yscale = 1,
        };

#ifdef WIDESCREEN_OPTION
const struct scanvideo_mode vga_mode_640x270_50_bem_1080p_non_widescreen =
        {
                .default_timing = &vga_timing_1920x1080_50,
                .pio_program = &video_24mhz_composable,
                .width = 960,
                .height = 270,
                .xscale = 2,
                .yscale = 1,
        };
static int8_t widescreen=-1, next_widescreen;
bool current_scanline_shifted;
#else
#define widescreen true
#endif
static void update_widescreen();

#define vga_mode vga_mode_640x270_50_bem_1080p
#endif

#ifdef DEBUG_SCANLINES
bool debug_half_line;
#define OFFSETOMAT 0
#endif
uint hacko(uint32_t scanline_id) {
    uint f = scanvideo_frame_number(scanline_id);
    uint l = scanvideo_scanline_number(scanline_id);

//    printf("DISPLAY SCANLINE ID %08x\n", scanline_id);

    // this function is done when we are about to scanout the scanline, so by this
    // time the rest of the code is somewhere ahead on the next frame
    if (f == scanvideo_frame_number(pos_tracking.half_line_scanline_id)) {
#ifndef HACK_MODE7_FIELD
        if (!non_interlaced_teletext) {
            if (scanline_id == pos_tracking.half_line_scanline_id) {
                return HALF_LINE_HEIGHT;
            } else if (l > scanvideo_scanline_number(pos_tracking.half_line_scanline_id)) {
                l--;
            }
        }
#endif
    }
    return HALF_LINE_HEIGHT * 2;
}
#else
const struct {
    uint width, height;
} video_mode = {
        .width = 640,
        .height = 256,
//                .xscale = 1,
//                .yscale = 1,
};
#endif

enum debug_scanline_type {
    SCANLINE_EMPTY = PICO_SCANVIDEO_PIXEL_FROM_RGB8(255, 255, 0),
    SCANLINE_AWAITING_ZERO = PICO_SCANVIDEO_PIXEL_FROM_RGB8(192, 0, 0),
    SCANLINE_INTERLACE_FIELD = PICO_SCANVIDEO_PIXEL_FROM_RGB8(0, 192, 64),
    SCANLINE_NORMAL = PICO_SCANVIDEO_PIXEL_FROM_RGB8(64, 64, 255),
};

#ifdef DEBUG_SCANLINES
#define DEBUG_SCANLINE_WORDS 12
int debug_scanline_number;



static void draw_debug_scanline_pixels(uint32_t *pixels, enum debug_scanline_type type) {
    uint16_t *p = (uint16_t *)pixels;
    uint c1 = debug_half_line ? PICO_SCANVIDEO_PIXEL_FROM_RGB8(255,255,255) : PICO_SCANVIDEO_PIXEL_FROM_RGB8(128,128,255);
    debug_half_line = 0;
    uint c2 = PICO_SCANVIDEO_PIXEL_FROM_RGB8(255,128,128);
    debug_scanline_number = pos_tracking.lines_since_vsync_pos - state.top_of_screen_line;
    p[0] = debug_scanline_number & 256 ? c2 : 0;
    p[1] = c1;
    p[2] = debug_scanline_number & 128 ? c2 : 0;
    p[3] = debug_scanline_number & 64 ? c2 : 0;
    p[4] = debug_scanline_number & 32 ? c2 : 0;
    p[5] = debug_scanline_number & 16 ? c2 : 0;
    p[6] = c1;
    p[7] = debug_scanline_number & 8 ? c2 : 0;
    p[8] = debug_scanline_number & 4 ? c2 : 0;
    p[9] = debug_scanline_number & 2 ? c2 : 0;
    p[10] = debug_scanline_number & 1 ? c2 : 0;
    p[11] = c1;
    p[12] = type;
    p[13] = c1;
    p[14] = type;
    p[15] = type;
    p[16] = c1;
    p[17] = pos_tracking.had_half_line ? c2 : 0;
    p[18] = c1;
    p[19] = pos_tracking.interlace_field ? c2 : 0;
    p[20] = c1;
    p[21] = pos_tracking.vdispen ? c2 : 0;
    p[22] = c1;
    p[23] = pos_tracking.vadj ? c2 : 0;
}
#endif
#ifdef ENABLE_BORDER
static uint current_border_color() {
    uint x = MIN(border_counter, 63) / 2;
    return PICO_SCANVIDEO_PIXEL_FROM_RGB5(x,x,x);
}

static bool top_bottom(int sl) {
    return !sl || sl == vga_mode.height - 1;// - (state.crtc_mode == TELETEXT); // why is teletext so far off!?
}
#endif

#if !X_GUI
uint do_end_of_scanline(uint16_t *buf16, int pixels);
#endif

static struct scanvideo_scanline_buffer *wrap_scanvideo_begin_scanline_generation(bool block) {
#ifndef X_GUI
    struct scanvideo_scanline_buffer * rc;
    if (!non_interlaced_teletext) {
        rc = scanvideo_begin_scanline_generation(block);
    } else {
        rc = scanvideo_begin_scanline_generation_linked(2, block);
    }
#if PIXELATED_PAUSE
    if (display_pps == PPS_SETUP_FRAME) {
        pps_line_number = scanvideo_scanline_number(rc->scanline_id);
        if (check_pps_line_number(pps_line_number/2) && !(pps_line_number & 1)) {
            pps_line_valid[pps_line_number/2] = pps_line_valid[pps_line_number/2] * 2 + 1;
            memset(pps_work_area, 0, 640);
        }
    }
#endif
#if defined(MODE_1080p) && defined(WIDESCREEN_OPTION)
    current_scanline_shifted = !widescreen;
    if (current_scanline_shifted) {
        for(scanvideo_scanline_buffer_t *buf = rc; buf; buf = buf->link) {
            *buf->data++ = COMPOSABLE_COLOR_RUN;
            *buf->data++ = (160-3-2) | (COMPOSABLE_RAW_2P << 16u);
            *buf->data++ = 0;
            buf->data_max -= 3;
#ifdef DISPLAY_MENU
            *buf->data2++ = COMPOSABLE_COLOR_RUN;
            *buf->data2++ = (160-3-2) | (COMPOSABLE_RAW_2P << 16u);
            *buf->data2++ = 0;
            buf->data2_max -= 3;
#endif
        }
    }
#endif
    if (debuggo) printf("New SL %08x\n", (uint)rc->scanline_id);
    return rc;
#else
    struct scanvideo_scanline_buffer *rc = x_gui_begin_scanline();
    rc->double_height = non_interlaced_teletext;
    if (!rc->double_height) rc->row1 = NULL;
    return rc;
#endif
}

static void wrap_scanvideo_end_scanline_generation(struct scanvideo_scanline_buffer *scanline_buffer) {
#if defined(MODE_1080p) && defined(WIDESCREEN_OPTION)
    if (current_scanline_shifted) {
        for(scanvideo_scanline_buffer_t *buf = scanline_buffer; buf; buf = buf->link) {
            buf->data -= 3;
            buf->data_max += 3;
            buf->data_used += 3;
#ifdef DISPLAY_MENU
            buf->data2 -= 3;
            buf->data2_max += 3;
            buf->data2_used += 3;
#endif
        }
    }
#endif
#if PIXELATED_PAUSE
    if (display_pps == PPS_SETUP_FRAME) {
        if (check_pps_line_number(pps_line_number/2) && (pps_line_number & 1u)) {
            pps_bitmap_line = pps_bitmap + (pps_line_number/2) * 80;
            pps_line_valid[pps_line_number/2] = pps_line_valid[pps_line_number/2] * 2 + 2;
            assert(((intptr_t)(pps_bitmap_line + 80))-((intptr_t)(pps_work_area)) < sizeof(mode7_pixels));
            for(uint i=0;i<80;i++) {
                uint pixel = (pps_work_area[i*2] >> 1u) & PP_MASK_1;
                pixel |= (pps_work_area[i*2+1] >> 1u) & PP_MASK_2;
                pps_bitmap_line[i] = pixel;
            }
        }
    }
#endif
#if !X_GUI
    scanvideo_end_scanline_generation(scanline_buffer);
#else
    scanline_buffer->half_line = pos_tracking.had_half_line;
    x_gui_end_scanline(scanline_buffer);
#endif
}

static void blank_scanline(enum debug_scanline_type type) {
#if !X_GUI
    uint32_t *p = scanline_buffer->data;
#ifndef DEBUG_SCANLINES
    uint16_t *buf16 = (uint16_t *)p;
    int pixels = draw_menu_background(buf16+2, scanvideo_scanline_number(scanline_buffer->scanline_id), 0);
#ifdef ENABLE_BORDER
    if (border_counter && !pixels) { // note !pixels for simplicity
        int sl = scanvideo_scanline_number(scanline_buffer->scanline_id);
        uint32_t color = current_border_color();
        uint16_t *px = (uint16_t *)p;
        if (top_bottom(sl)) {
            px[0] = COMPOSABLE_COLOR_RUN;
            px[1] = current_border_color();
            px[2] = video_mode.width - 3;
            px[3] = COMPOSABLE_RAW_1P;
            px[4] = 0;
            px[5] = COMPOSABLE_EOL_ALIGN;
            scanline_buffer->data_used = 3;
        } else {
            px[0] = COMPOSABLE_RAW_1P;
            px[1] = color;
            px[2] = COMPOSABLE_COLOR_RUN;
            px[3] = 0;
            px[4] = video_mode.width - 5;
            px[5] = COMPOSABLE_RAW_2P;
            px[6] = color;
            px[7] = 0;
            px[8] = COMPOSABLE_EOL_SKIP_ALIGN;
            scanline_buffer->data_used = 5;
        }
    } else
#endif
    {
        if (pixels) {
            p[0] = COMPOSABLE_RAW_RUN;
            scanline_buffer->data_used = do_end_of_scanline(buf16, pixels);
        } else {
            p[0] = COMPOSABLE_RAW_1P;
            p[1] = COMPOSABLE_EOL_SKIP_ALIGN;
            scanline_buffer->data_used = 2;
        }
    }
#else
    p[0] = COMPOSABLE_RAW_RUN;
    p[1] = DEBUG_SCANLINE_WORDS * 2 + 1;
    p[2] = 0;
    p[3 + DEBUG_SCANLINE_WORDS] = COMPOSABLE_RAW_1P;
    p[4 + DEBUG_SCANLINE_WORDS] = COMPOSABLE_EOL_SKIP_ALIGN;
    scanline_buffer->data_used = 5 + DEBUG_SCANLINE_WORDS;
    draw_debug_scanline_pixels(p + 3, type);
#endif
#if DISPLAY_MENU
    draw_menu_foreground(scanline_buffer);
#endif
#else
    if (scanline_buffer->row0) {
        memset(scanline_buffer->row0, 0, 640 * 2);
    }
    if (scanline_buffer->row1) {
        memset(scanline_buffer->row1, 0, 640 * 2);
    }
    // todo menu
#endif
}

static struct semaphore core1_sem;
static struct mutex queue_mutex;

void run_record_reader();

int run_buffer(uint32_t *data_buf, int read_pos, int limit);

#ifndef MODE_1280
#define VIDEO_SETUP_ON_CORE1
#endif

static void setup_video() {
#if !X_GUI
    scanvideo_setup(&vga_mode);
#else
    printf("TODO: x_gui_setup_video()\n");
#endif

#if DISPLAY_MENU
    menu_init();
#if PICO_ON_DEVICE
    masked_run_aligned_cmd = pio_add_program(pio0, &masked_run_aligned_program);
#elif !X_GUI
    masked_run_aligned_cmd = 22;
    void simulate_composable_masked_run_aligned(const uint16_t **dma_data, uint16_t **pixels, int32_t max_pixels, bool overlay);
    scanvideo_set_simulate_composable_cmd(masked_run_aligned_cmd, simulate_composable_masked_run_aligned);
#endif
#endif
#if defined(MODE_1080p) && !defined(WIDESCREEN_OPTION)
    update_widescreen();
#endif

#if !X_GUI
    scanvideo_set_scanline_repeat_fn(hacko);
    scanvideo_timing_enable(true);
    video_mode = scanvideo_get_mode();
#endif
}

void video_core1() {
#ifdef VIDEO_SETUP_ON_CORE1
    setup_video();
#endif
#if USE_USB_KEYBOARD
#ifdef USB_SETUP_ON_CORE1
    extern void usb_host_hid_init();
    usb_host_hid_init();
#endif
#endif
    sem_release(&core1_sem);
#ifdef DISPLAY_WIRE
    run_record_reader();
#endif
}


ALLEGRO_DISPLAY *video_init(void) {
#ifndef VIDEO_SETUP_ON_CORE1
    setup_video();
#endif

    mode7.p[0] = mode7_chars;
    mode7.p[1] = mode7_charsi;

    nula_reset();

    state.top_of_screen_line = 32;
#ifndef SINGLE_CORE
    sem_init(&core1_sem, 0, 1);
    mutex_init(&queue_mutex);
    multicore_launch_core1(video_core1);
    sem_acquire_blocking(&core1_sem);
#endif
    return &_display;
}

void video_close() {

}

static_if_display_wire void effect_crtc_reset() {
    mode7.sc = 0;
    nula_reset();
    mode7_color_updated();
}

void mode7_row_end() {
    mode7.col = 7;
    mode7.bg = 0;
    mode7.holdchar = 0;
    mode7.heldchar = 0x20;
    mode7.flash = 0;
    mode7.sep = 0;
    mode7.gfx = 0;
    mode7.p[0] = mode7_chars;
    mode7.p[1] = mode7_charsi;
    mode7.heldp[0] = mode7.p[0];
    mode7.heldp[1] = mode7.p[1];
    mode7_color_updated();

#ifndef NO_USE_NULA_PIXEL_HSCROLL
#error unsupported
    if (state.crtc_mode != TELETEXT) {
        // NULA left edge
        nula_left_edge = pos_tracking.scrx + char_width_pixels;

        // NULA left cut
        nula_left_cut = nula_left_edge + nula_left_blank * char_width_pixels;

        // NULA horizontal offset - "delay" the pixel clock
        for (int c = 0; c < nula_horizontal_offset * state.crtc_mode; c++, pos_tracking.scrx++) {
            put_pixel(pos_tracking.scrx + char_width_pixels, colblack);
        }
    }
#endif

    if (!pos_tracking.vadj) {
        mode7.sc++;
        if (mode7.sc == 10) {
            if (mode7.nextdbl)
                mode7.nextdbl = 0;
            else
                mode7.nextdbl = mode7.wasdbl;
            mode7.sc = 0;
        }
    }
    mode7.dbl = mode7.wasdbl = 0;
}


static void set_video_frame_ended() {
    pos_tracking.xpos = 0;
    pos_tracking.lines_since_vsync_pos = 0;
    pos_tracking.first_display_enabled_line = 0;
    pos_tracking.had_half_line_last = pos_tracking.had_half_line;
    pos_tracking.half_line_done = 0;
    pos_tracking.half_line_scanline_added = 0;
    pos_tracking.had_half_line = 0;
    pos_tracking.crtc_frame++;
#if ENABLE_FRAME_SKIP
    if (skip_counter++ >= frame_skip_count) {
        skip_frame = false;
        skip_counter = 0;
    } else {
        skip_frame = true;
    }
#endif
//    pos_tracking.firsty = 65535;
//    pos_tracking.lasty  = 0;
}


static void check_draw_cursor() {
    if (pos_tracking.cdraw) {
        if (pos_tracking.cursor_on && (state.ula_ctrl & cursorlook[pos_tracking.cdraw])) {
            if (state.crtc_mode != TELETEXT) {
                assert(pos_tracking.xpos);
                pos_tracking.xpos--;
                state.crtc_mode == HIGH_FREQ ? draw_tile8_hi(gen_cursor_tile8(0)) : draw_tile8_lo(gen_cursor_tile8(0));
            } else {
//                assert(pos_tracking.xpos > 1); // may not be true because of hsync_pos
                pos_tracking.xpos -= 2;
                // black magic
                if (!non_interlaced_teletext) {
                    const struct maybe_aligned_8_pixels *c1 = (struct maybe_aligned_8_pixels *) gen_cursor_tile8(-16);
                    const struct maybe_aligned_8_pixels *c2 = (struct maybe_aligned_8_pixels *) gen_cursor_tile8(-8);
                    draw_tiles8x2(c1, c2);
                    pos_tracking.xpos += 2;
                } else {
                    const struct aligned_8_pixels *c1 = gen_cursor_tile8(-16);
                    const struct aligned_8_pixels *c2 = gen_cursor_tile8(-8);
                    draw_tiles8x2x2_aligned(c1, c2, c1, c2);
                }

                pos_tracking.xpos++;
            }
        }
        pos_tracking.cdraw++;
        if (pos_tracking.cdraw == 7)
            pos_tracking.cdraw = 0;
    }
}

static void __time_critical_func(draw_bytes_teletext_eol)(__unused const uint8_t *dat, int len) {
    draw_bytes_teletext(NULL, len);
}

static_if_display_wire void __time_critical_func(effect_vsync_pos)(int interline, int interlline, bool pixelated_pause) {
    DEBUG_PINS_XOR(scanline, 2);
    cmd_trace("%d: VSYNC_POS: il=%d ill=%d\n", pos_tracking.lines_since_vsync_pos, interline, interlline);

#ifdef ENABLE_BORDER
    if (border_counter) border_counter--;
#endif
#if DISPLAY_MENU
    menu_display_blanking();
#endif
#ifdef BLANKING_FRAMES
    if (blanking_frames) blanking_frames--;
#endif

#if PIXELATED_PAUSE
    switch (display_pps) {
        case PPS_SETUP_FRAME:
            display_pps = PPS_ACTIVE;
            // fall thrue
        case PPS_ACTIVE:
            while (!display_pps_unpause) {
                pps_frame();
#ifndef DISPLAY_WIRE
                ALLEGRO_EVENT event;
                al_wait_for_event(NULL, &event);
                if (clear_pixelated_pause) {
                    display_pps_unpause = true;
                    cpu_pps = PPS_NONE;
                }
#endif
            }
            if (display_pps_unpause) {
                // we trashed this
                mode7.need_new_lookup = 1;
                set_all_tiles_dirty();
                display_pps = PPS_NONE;
                display_pps_unpause = false;
                blanking_frames = 1; // we have sync issues coming out, so just do a blank frame
            }
            return;
        default:
            if (pixelated_pause) {
                display_pps = PPS_SETUP_FRAME;
                memset(pps_line_valid, 0, PPS_VALID_LINE_COUNT);
                int total = 0;
                for(int i=0;i<7;i++) {
                    total += grey_pixels[i];
                }
                for(int i=0;i<count_of(grey_pixels)-7;i++) {
                    total += grey_pixels[i+7];
                    pps_mode7_alpha[i] = total/8;
                    total -= grey_pixels[i];
                }
                set_all_tiles_dirty();
            }
            break;
    }
#endif

    vpos_offset = next_vpos_offset;
#if DISPLAY_MENU
    reflect_vpos_offset(vpos_offset);
#if ENABLE_FRAME_SKIP
    frame_skip_count = next_frame_skip_count;
    reflect_frame_skip_count(frame_skip_count);
#endif
#endif
#if defined(MODE_1080p) && defined(WIDESCREEN_OPTION)
    if (next_widescreen != widescreen) {
        widescreen = next_widescreen;
        reflect_widescreen(widescreen);
        update_widescreen();
    }
#endif
    vtotal_displayed = vtotal = 0;

    set_video_frame_ended();
    mode7.flashtime++;
    if ((mode7.flashon && mode7.flashtime == 32) || (!mode7.flashon && mode7.flashtime == 16)) {
        mode7.flashon = !mode7.flashon;
        mode7.flashtime = 0;
    }
    mode7.sc = 0;
}

void effect_display_reset() {
    // todo reset a struct, otherwise we could prune
#ifndef NO_USE_NULA_PIXEL_HSCROLL
    nula_left_cut = 0;
    nula_left_edge = 0;
    nula_left_blank = 0;
    nula_horizontal_offset = 0;
#endif
    memset(&pos_tracking, 0, sizeof(pos_tracking));
    next_vpos_offset = 0;
//    next_non_interlaced_teletext = false;
}

static_if_display_wire void __time_critical_func(effect_hsync_pos)() {
    cmd_trace("%d: HSYNC_POS\n", pos_tracking.lines_since_vsync_pos);
    pos_tracking.xpos = 0;
//    pos_tracking.lines_since_vsync_pos++;
//    if (pos_tracking.lines_since_vsync_pos >= 384) {
//        set_video_frame_ended();
//    }
}

static_if_display_wire void __time_critical_func(effect_cdraw)(int cdraw) {
    pos_tracking.cdraw = cdraw;
}

#ifdef X_GUI

static bool x_gui_skip() {
    return !scanline_buffer || !scanline_buffer->row0;
}

#endif
static_if_display_wire void __time_critical_func(effect_displayed_chars)(const uint8_t *dat, int count, int cdraw_pos) {
#ifdef ENABLE_FRAME_SKIP
    if (skip_frame) return;
#endif
#if X_GUI
    if (x_gui_skip()) return;
#endif
    if (pos_tracking.xpos + count > 80) {
        count = 80 - pos_tracking.xpos;
        if (count <= 0) return;
    }
    current_displayed_chars_fn(dat, count);
#ifdef DEBUG_DISPLAYED_CHAR
    check_draw_cursor();
#else
    if (pos_tracking.cdraw || cdraw_pos) {
        if (cdraw_pos) {
            count -= cdraw_pos - 1;
            if (count > 0) {
                effect_cdraw(3 - (state.crtc8 >> 6));
            }
        }
        if (count > 0) {
            uint save = pos_tracking.xpos;
            pos_tracking.xpos -= count - 1;
            for (int i = 0; i < count; i++) {
                check_draw_cursor();
                if (!pos_tracking.cdraw) break;
                pos_tracking.xpos++;
            }
            pos_tracking.xpos = save;
        }
    }
#endif
}

static_if_display_wire void __time_critical_func(effect_crtc_write)(uint reg, uint value) {
    if (reg == CRTC_HTOTAL) {
        state.crtc_htotal = value;
    } else if (reg == CRTC_HSYNC_POS) {
        state.crtc_hsync_pos = value;
    } else if (reg == CRTC_SYNC_WIDTH) {
        state.crtc_hsync_width = value & 0xfu;
    } else if (reg == CRTC_MODE_CTRL) {
        state.crtc8 = value;
        update_displayed_char_fn();
        set_intern_dtype(vid_dtype_user);
    } else {
        if (reg == CRTC_VSYNC_POS) {
//            printf("VSYNCPOS %d\n", value);
        }
        return;
    }
    update_hpos();
}

void set_intern_dtype(enum vid_disptype dtype) {
    if (state.crtc_mode == TELETEXT && (state.crtc8 & 1))
        dtype = VDT_INTERLACE;
    else if (dtype == VDT_INTERLACE && !(state.crtc8 & 1))
        dtype = VDT_SCALE;
    vid_dtype_intern = dtype;
}

static_if_display_wire void __time_critical_func(effect_row_start)(int vdispen, int vadj, int interline, int interlline,
                                                             int cursoron, int sc) {
    cmd_trace("%d: ROW_START: disp=%d adj=%d il=%d ill=%d\n", pos_tracking.lines_since_vsync_pos, vdispen, vadj,
              interline, interlline);
    if (vdispen && !pos_tracking.vdispen) {
        pos_tracking.first_display_enabled_line = pos_tracking.lines_since_vsync_pos;
    }
    pos_tracking.vdispen = vdispen;
    pos_tracking.vadj = vadj != 0;

    pos_tracking.interlace_field = interlline;
    pos_tracking.cursor_on = cursoron;
    pos_tracking.sc = sc;

    // (no longer true: only actually necessary when there are blank lines (i.e. >8 high characters) (e.g. mode 3, 6))
    // ^ i made the telext_eol change the function - it could put it back afterwards, but since this is here, i'm fine with that
    update_displayed_char_fn();

#ifdef ENABLE_FRAME_SKIP
    if (skip_frame)
        return;
#endif
    if (state.crtc_htotal) {
        if (!scanline_buffer) {
            scanline_buffer = wrap_scanvideo_begin_scanline_generation(true);
        }
        int crtc_line;
        int wibble = 0;
        do {
            crtc_line = pos_tracking.lines_since_vsync_pos - state.top_of_screen_line + vpos_offset;

            int scanvideo_line = scanvideo_scanline_number(scanline_buffer->scanline_id);
            // todo better test here... we seem to have an issue with crtc_line being max and scanvideo_line being 0
//            if (scanvideo_line && crtc_line > scanvideo_line && crtc_line < video_mode.height) {
            int max = video_mode.height;
            static uint16_t last_frame_num;
            int scanvideo_frame_num = scanvideo_frame_number(scanline_buffer->scanline_id);
            bool new_frame = false;
            if (scanvideo_frame_num != last_frame_num) {
                new_frame = true;
                last_frame_num = scanvideo_frame_num;
            }
            if ((!new_frame && crtc_line > scanvideo_line && crtc_line < max) ||
                (crtc_line < 0 && scanvideo_line > 80)) { // todo arbitrary
                DEBUG_PINS_SET(scanline, 1);

//#define NO_HOLD
#ifndef NO_HOLD
                if (debuggo) {
                    printf("BS crtc %d scanvideo %d\n", crtc_line, scanvideo_line);
                }
                blank_scanline(SCANLINE_AWAITING_ZERO);
                wrap_scanvideo_end_scanline_generation(scanline_buffer);
                scanline_buffer = wrap_scanvideo_begin_scanline_generation(true);
#endif
                DEBUG_PINS_CLR(scanline, 1);
            } else {
                break;
            }
        } while (++wibble < 20);

        if (pos_tracking.had_half_line && !pos_tracking.half_line_scanline_added) {
            if (!non_interlaced_teletext) {
#ifndef HACK_MODE7_FIELD
                if (debuggo) {
                    printf("HL crtc %d scanvideo %08x\n", crtc_line, (uint)scanline_buffer->scanline_id);
                }
                blank_scanline(SCANLINE_INTERLACE_FIELD);
                pos_tracking.half_line_scanline_id = scanline_buffer->scanline_id;
//            printf("HALF TRACKING SCANLINE ID %08x\n", pos_tracking.half_line_scanline_id);
                wrap_scanvideo_end_scanline_generation(scanline_buffer);
                scanline_buffer = wrap_scanvideo_begin_scanline_generation(true);
                pos_tracking.half_line_scanline_added = true;
#endif
            }
        }

#if !X_GUI
        current_scanline_pixels = (uint16_t *) (scanline_buffer->data + 1);
        current_scanline_pixels2 = non_interlaced_teletext && scanline_buffer->link ? (uint16_t *)(scanline_buffer->link->data + 1) : NULL;
        assert(scanline_buffer->data_max >= current_scanline_pixel_width / 2 + 3);
#else
        current_scanline_pixels = scanline_buffer->row0;
//        assert((non_interlaced_teletext != 0) ^ (scanline_buffer->row1 == 0));
        current_scanline_pixels2 = scanline_buffer->row1;
#endif
        current_scanline_pixel_width = 640;

        // todo there was something about this i meant to fix (perhaps doing the hpos set here not earlier)
        if (pos_tracking.hpos > 0) {
#ifdef X_GUI
            if (x_gui_skip()) return;
#endif
            if (state.crtc_mode == HIGH_FREQ) {
                draw_bytes_black_hi(NULL, pos_tracking.hpos);
            } else {
                draw_bytes_black_lo(NULL, pos_tracking.hpos);
            }
        } else {
            pos_tracking.xpos = pos_tracking.hpos;
        }
    }
}

#ifdef ENABLE_BORDER
static int16_t add_border( uint16_t *buf16, int16_t pixels) {
    if (border_counter) {
        if (pixels < 640) {
            memset(buf16 + pixels, 0, (640-pixels)*2);
            pixels = 640;
        }
        int sl = scanvideo_scanline_number(scanline_buffer->scanline_id);
        if (top_bottom(sl)) {
            uint c = current_border_color() * 0x10001;
            for(uint i=0;i<320;i++) {
                ((uint32_t *)buf16)[i] = c;
            }
        } else {
            buf16[0] = current_border_color();
            buf16[639] = current_border_color();
        }
    }
    return pixels;
}
#endif

void __time_critical_func(effect_row_end)(int hc, bool full_line) {
    cmd_trace("%d: ROW_END: full=%d\n", pos_tracking.lines_since_vsync_pos, full_line);
    if (full_line && state.crtc_mode == TELETEXT) { // todo added this teletext check; seems reasonable
        DEBUG_PINS_SET(teletext, 1);
        mode7_row_end();
        DEBUG_PINS_CLR(teletext, 1);
    }

    if (!full_line) {
//        pos_tracking.lines_since_vsync_pos--;
        pos_tracking.had_half_line = (state.crtc_mode == TELETEXT || !force_tv_comma_1);
        pos_tracking.xpos = 0;
        return;
    }

#ifdef ENABLE_FRAME_SKIP
    if (scanline_buffer && !skip_frame)
#else
    if (scanline_buffer)
#endif
    {
        int crtc_line = pos_tracking.lines_since_vsync_pos - state.top_of_screen_line + vpos_offset;
        if (debuggo) printf("ROW_END crtc %d sl %d\n", crtc_line, scanvideo_scanline_number(scanline_buffer->scanline_id));
        int max = video_mode.height;
        if (crtc_line >= 0 && crtc_line < max - pos_tracking.had_half_line && !blanking_frames) {
            if (debuggo) printf("  in range\n");
            DEBUG_PINS_XOR(sync, 2);
            int16_t pixels = pos_tracking.end_xpos * char_width_pixels;
#if DISPLAY_MENU
            draw_menu_foreground(scanline_buffer);
#endif

            if (pixels > 0) {
#if !X_GUI
                uint16_t *buf16 = (uint16_t *) scanline_buffer->data;
                if (pixels > 640) pixels = 640;
#if PIXELATED_PAUSE
                if (display_pps == PPS_SETUP_FRAME) {
                    // do black out here rather than in rendering (which is too slow in mode 7)
                    // todo note right now we are still drawing black in graphics modes since I'm seeing
                    //  some colored lines still
                    pixels = 0;
                }
#endif
#ifdef ENABLE_BORDER
                pixels = add_border(buf16 + 2, pixels);
#endif
                buf16[0] = COMPOSABLE_RAW_RUN;
#if DISPLAY_MENU
                pixels = draw_menu_background(buf16 + 2, scanvideo_scanline_number(scanline_buffer->scanline_id), pixels);
#endif
#ifdef DEBUG_SCANLINES
                if (pixels > DEBUG_SCANLINE_WORDS * 2 + 4) {
                    draw_debug_scanline_pixels(scanline_buffer->data + 3, SCANLINE_NORMAL);
                }
#endif
                // do this after the end of the background erase which may be longer than we are
                scanline_buffer->data_used = do_end_of_scanline(buf16, pixels);
                if (non_interlaced_teletext) {
                    if (scanline_buffer->link) {
#if PICO_ON_DEVICE
                        scanline_buffer->link_after = 2;
#else
                        scanline_buffer->link_after = 1;
#endif
#if DISPLAY_MENU
                        // no need to draw, the content is identical!
                        // draw_menu_foreground(scanline_buffer->link);
                        memcpy(scanline_buffer->link->data2, scanline_buffer->data2, scanline_buffer->data2_used * 4);
                        scanline_buffer->link->data2_used = scanline_buffer->data2_used;
#endif

                        uint16_t *buf16_2 = (uint16_t *) scanline_buffer->link->data;
#ifdef ENABLE_BORDER
                        add_border(buf16_2 + 2, pixels);
#endif
                        buf16_2[0] = COMPOSABLE_RAW_RUN;
#if DISPLAY_MENU
                        pixels = draw_menu_background(buf16_2 + 2, scanvideo_scanline_number(scanline_buffer->scanline_id), pixels);
#endif
                        scanline_buffer->link->data_used = do_end_of_scanline(buf16_2, pixels);
                    }
                }
#else
                if (pixels < 640 && scanline_buffer->row0) {
                    memset(scanline_buffer->row0 + pixels, 0, (640 - pixels) * 2);
                    if (scanline_buffer->row1) {
                        memset(scanline_buffer->row1 + pixels, 0, (640 - pixels) * 2);
                    }
                }
#endif
            } else {
                blank_scanline(SCANLINE_EMPTY);
            }
//                printf("PIXELS %d USED %d\n", pixels, scanline_buffer->data_used);
            if (debuggo) printf("Regular %d eof\n", scanvideo_scanline_number(scanline_buffer->scanline_id));
            // if we are here then this is the right buffer
            wrap_scanvideo_end_scanline_generation(scanline_buffer);
            scanline_buffer = NULL;
        } else {
            if (debuggo) printf("  out of range\n");

            DEBUG_PINS_XOR(sync, 4);
            pos_tracking.xpos = 0;
//            if (pos_tracking.end_xpos)
//                printf("EXTRA %d %d\n", crtc_line, pos_tracking.end_xpos);
        }
    } else {
//        printf("WAHA\n");
    }

    if (pos_tracking.vdispen) vtotal_displayed++;
    vtotal++;

    pos_tracking.lines_since_vsync_pos++;
    if (pos_tracking.lines_since_vsync_pos >= 384) {
        set_video_frame_ended();
#if DISPLAY_MENU
        menu_display_blanking();
#endif
    }

}

static_if_display_wire void __time_critical_func(effect_hdisplay_count_pos)(bool dispen) {
    // todo why would dispen != vdispen?
    if (dispen && state.crtc_mode == TELETEXT) {
        current_displayed_chars_fn = draw_bytes_teletext_eol;
#ifdef DEBUG_DISPLAYED_CHAR
        effect_displayed_chars(zeros16, 1, 0); // parameter is actually ignored
        effect_displayed_chars(zeros16, 1, 0); // parameter is actually ignored
        effect_displayed_chars(zeros16, 1, 0); // parameter is actually ignored
#else
        effect_displayed_chars(zeros16, 3, 0); // parameter is actually ignored
#endif
    }
    pos_tracking.end_xpos = pos_tracking.xpos;
}

#ifdef DISPLAY_WIRE
// todo size
#define MIN_WIRE_SIZE 256

static uint32_t data_buf[1024];
static int queue_write_pos;

// protected by sem
static int queue_read_pos;
static int queue_read_limit;

struct scanvideo_scanline_buffer fake_buffer = {
        .data = data_buf,
        .data_max = 1,
};

struct scanvideo_scanline_buffer *__time_critical_func(commit_record_buffer)(struct scanvideo_scanline_buffer *buffer, bool abort) {
    if (!abort) {
        assert(buffer == &fake_buffer);
        assert(buffer->data >= data_buf);
        assert(buffer->data + buffer->data_used < data_buf + count_of(data_buf));
    } else {
        if (!buffer) {
            buffer = &fake_buffer;
        }
        buffer->data[0] = REC_ABORT << 16u;
        buffer->data_used = 1;
    }
#ifdef SINGLE_CORE
    // for now skip entirely
//#if !PICO_ON_DEVICE
    run_buffer(buffer->data, 0, buffer->data_used);
//#endif
#else
//    printf("Writing %d->%d\n", queue_write_pos, queue_write_pos + buffer->data_used);
    queue_write_pos += buffer->data_used;
    if (queue_write_pos + MIN_WIRE_SIZE + 1> count_of(data_buf)) {
        assert(buffer->data_used != buffer->data_max);
        buffer->data[buffer->data_used++] = REC_BUFFER_WRAP << 16;
        queue_write_pos = 0;
    }
    bool first = true;
    bool wait = false;
    do {
        mutex_enter_blocking(&queue_mutex);
        if (first && queue_write_pos) {
            queue_read_limit = queue_write_pos;
            __sev();
        }
        int available = queue_read_pos - queue_write_pos;
        wait = (available > 0 && available < MIN_WIRE_SIZE);
        mutex_exit(&queue_mutex);
        if (wait) {
#if PIXELATED_PAUSE
            if (cpu_pps && display_pps == PPS_ACTIVE) {
                while (!clear_pixelated_pause) {
                    ALLEGRO_EVENT event;
                    al_wait_for_event(NULL, &event);
                    sleep_ms(20);
                }
                display_pps_unpause = true;
                cpu_pps = PPS_NONE;
            }
#endif
            __wfe();
        }
    } while (wait);
#endif
    fake_buffer.data = data_buf + queue_write_pos;
    fake_buffer.data_used = 0;
    fake_buffer.data_max = MIN_WIRE_SIZE;
    return &fake_buffer;
}

void __time_critical_func(run_record_reader)() {
    int read_pos = 0;
    int limit = 0;
    do {
        mutex_enter_blocking(&queue_mutex);
        if (read_pos != -1) {
            queue_read_pos = read_pos;
            __sev();
            read_pos = -1;
        }
        if (queue_read_pos != queue_read_limit) {
            read_pos = queue_read_pos;
            limit = queue_read_limit;
        }
        mutex_exit(&queue_mutex);
        if (read_pos == -1) {
            __wfe();
            continue;
        }
//        printf("Reading %d->%d\n", read_pos, limit);
        read_pos = run_buffer(data_buf, read_pos, limit);
    } while (true);
}

int __time_critical_func(run_buffer)(uint32_t *data_buf, int read_pos, int limit) {
    DEBUG_PINS_SET(core_use, 2);
    while (read_pos != limit) {
        uint w = data_buf[read_pos++];
//        int scanline = scanline_buffer ? scanvideo_scanline_number(scanline_buffer->scanline_id) : 0;
//        printf("%d: %08x\n", scanline, w);
        switch (w >> 16u) {
            case REC_DRAW_BYTES: {
                int len = w & 0xffu;
                assert(len);
#ifdef DEBUG_DISPLAYED_CHAR
                for(int i=0;i<len;i++) {
                    if ((i + 1) == (uint8_t)(w>>8)) {
                        effect_cdraw(3 - (state.crtc8 >> 6));
                    }
                    effect_displayed_chars( ((const uint8_t *)(data_buf + read_pos)) + i, 1, 0);
                }
#else
                effect_displayed_chars( (const uint8_t *)(data_buf + read_pos), len, (uint8_t)(w>>8));
#endif
                read_pos += (len + 3u) / 4;
                break;
            }
            case REC_DISPLAY_RESET:
                effect_display_reset();
                break;
            case REC_CRTC_REG:
                effect_crtc_write((w >> 8u) & 0xffu, w & 0xffu);
                break;
            case REC_ULA_REG:
                effect_ula_write((w >> 8u) & 0xffu, w & 0xffu);
                break;
            case REC_ROW_START: {
                if (state.crtc_htotal) {
                    //end_scanline_buffer();
#if false && PICO_ON_DEVICE
                    int foo = (int)(mm_xip_ctrl->ctr_acc - mm_xip_ctrl->ctr_hit);
                    if (foo > 100)
                        printf("%d %d\n", foo, mm_xip_ctrl->ctr_acc);
                    // Clear counters (write any value to clear)
                    mm_xip_ctrl->ctr_acc = 1;
                    mm_xip_ctrl->ctr_hit = 1;
#endif
                }

                struct row_start_params p;
                p.val = w & 0xffffu;
                effect_row_start(p.vdispen, p.vadj, p.interline, p.interlline, p.cursoron, p.sc);
                break;
            }
            case REC_ROW_END_FULL:
                effect_row_end(w & 0xffu, true);
                break;
            case REC_ROW_END_HALF:
                effect_row_end(w & 0xffu, false);
                break;
            case REC_HDISPLAY_COUNT_POS:
                effect_hdisplay_count_pos(w & 1u);
                break;
            case REC_HSYNC_POS:
                effect_hsync_pos();
                break;
            case REC_VSYNC_POS:
                effect_vsync_pos((w >> 8u) & 0x7fu, w & 0xffu, (w >> 15u)&1);
//                printf("Vsync %d\n", (int)(us_since_boot(get_absolute_time()) / 1000));
                break;
            case REC_CDRAW:
                effect_cdraw(w & 0xffu);
                break;
            case REC_BUFFER_WRAP:
                read_pos = 0;
                break;
            case REC_ABORT:
                effect_crtc_reset();
                //end_of_frame = false;
                break;
#ifdef USE_CORE1_SOUND
            case REC_SOUND_SYNC:
                core1_sound_sync(w & 0xffffu);
                break;
            case REC_SN_76489:
                sn76489_sound_event(w & 0xffu);
                break;
#endif
        }
    }
    DEBUG_PINS_CLR(core_use, 2);
    return read_pos;
}

void __time_critical_func(squash_record_buffer)(struct scanvideo_scanline_buffer *buffer) {
    // just copy across everything other than pixels
    buffer->data_used = 0;
    for(int i=0; i<buffer->data_max; i++) {
        if (buffer->data[i] >> 8) {
            buffer->data[buffer->data_used++] = buffer->data[i];
        } else {
            int len = buffer->data[i] & 0xffu;
            i += (len + 3u) / 4;
        }
    }
    printf("SQUASH %d -> %d\n", buffer->data_max, buffer->data_used);
}
#endif

#if PICO_NO_HARDWARE

void simulate_composable_masked_run_aligned(const uint16_t **dma_data, uint16_t **pixels, int32_t max_pixels,
                                            bool overlay) {
    assert(!(3 & (intptr_t) *dma_data));

    uint16_t color = *(*dma_data)++;
    int16_t count = *(*dma_data)++;
    uint bits;
    uint bit_count = 0;
    while (count-- >= 0) {
        if (!bit_count) {
            bits = (*(*dma_data)++);
            bits |= ((*(*dma_data)++) << 16u);
            bit_count = 31;
        } else {
            bits >>= 1;
            bit_count--;
        }
        if (bits & 1) {
            (*pixels)[0] = color;
            (*pixels)[1] = color;
        }
        (*pixels) += 2;
    }
//    assert(ok);
    assert(!(3 & (intptr_t) (*dma_data))); // should end on dword boundary
//    assert(!expected_width || pixels == pixel_buffer + expected_width); // with the correct number of pixels (one more because we stick a black pixel on the end)
}

#endif

#if PIXELATED_PAUSE
void pps_frame() {
#if DISPLAY_MENU
    menu_display_blanking();
#endif

    int sl_num_start = -1;
    do {
        if (!scanline_buffer) {
            scanline_buffer = wrap_scanvideo_begin_scanline_generation(true);
        }
        int sl_num = scanvideo_scanline_number(scanline_buffer->scanline_id);
        if (scanvideo_scanline_number(scanline_buffer->scanline_id) == sl_num_start) {
            // done a frame's worth
            return;
        }
        if (sl_num_start == -1) {
            sl_num_start = sl_num;
        }
        if (scanline_buffer->link) {
            scanline_buffer->link_after = 4; // we don't want to link
        }
#if DISPLAY_MENU
        draw_menu_foreground(scanline_buffer);
#endif
        uint16_t *buf16 = (uint16_t *) scanline_buffer->data;
        buf16[0] = COMPOSABLE_RAW_RUN;
//        if (!(sl_num & 1)) {
        int pixels;
        uint32_t *wa = (uint32_t *) (buf16 + 2);
        if (check_pps_line_number(sl_num/2) && pps_line_valid[sl_num/2] == 4) { // 1 *2 + 2 .. means we had things in the right order
            pps_bitmap_line = pps_bitmap + (sl_num / 2) * 80;
            for (uint i = 0; i < 80; i++) {
                uint32_t pixel = pps_bitmap_line[i];
                pixel = (pixel << 16) | pixel;
                wa[0] = wa[1] = wa[2] = wa[3] = pixel;
                wa += 4;
            }
            pixels = 640;
        } else {
//            printf("Skipping %d: %d\n", sl_num/2, pps_line_valid[sl_num/2]);
            wa[0] = wa[1] = 0;
            pixels = 4;
        }
#if DISPLAY_MENU
        pixels = draw_menu_background(buf16 + 2, sl_num, pixels);
#endif
        scanline_buffer->data_used = do_end_of_scanline(buf16, pixels);
        wrap_scanvideo_end_scanline_generation(scanline_buffer);
        scanline_buffer = NULL;
    } while (true);
}


#endif

#if !X_GUI
uint do_end_of_scanline(uint16_t *buf16, int pixels) {
    buf16[1] = buf16[2];
    buf16[2] = pixels - 3;
    // do this after the end of the background erase which may be longer than we are
    buf16[pixels + 2] = COMPOSABLE_RAW_1P;
    buf16[pixels + 3] = 0;
    buf16[pixels + 4] = COMPOSABLE_EOL_SKIP_ALIGN;
    uint used = pixels + 2 + 4;
    assert(!(used & 1));
    return used >> 1;
}
#endif

#ifdef MODE_1080p
#ifdef WIDESCREEN_OPTION
void set_widescreen(bool _widescreen) {
    next_widescreen = _widescreen;
}
extern bool video_24mhz_composable_adapt_for_mode(const scanvideo_pio_program_t *program, const scanvideo_mode_t *mode,
                                                  scanvideo_scanline_buffer_t *missing_scanline_buffer,
                                                  uint16_t *modifiable_instructions);

#endif

void update_widescreen() {
    uint menu_delay = widescreen ? 8 : 4;
    // stretch the menu overlay code
    pio0->instr_mem[masked_run_aligned_cmd + masked_run_aligned_offset_delay1] =
            pio_encode_delay(menu_delay) |
            masked_run_aligned_program.instructions[masked_run_aligned_offset_delay1];
    pio0->instr_mem[masked_run_aligned_cmd + masked_run_aligned_offset_delay2] =
            pio_encode_delay(menu_delay) |
            masked_run_aligned_program.instructions[masked_run_aligned_offset_delay2];

#ifdef WIDESCREEN_OPTION
    // huge hack for now, using internals of video, since there is no mode update method
    const scanvideo_pio_program_t *program = vga_mode.pio_program;
    scanvideo_scanline_buffer_t dummy_buffer;
    uint length = program->program->length;
    uint16_t new_program[length];
    memcpy(new_program, program->program->instructions, length * 2);
    video_24mhz_composable_adapt_for_mode(program, widescreen ? &vga_mode_640x270_50_bem_1080p : &vga_mode_640x270_50_bem_1080p_non_widescreen,
                                          &dummy_buffer, new_program);
    // we assume loaded at 0
    for(uint i=0;i<length;i++) {
        pio0->instr_mem[i] = new_program[i];
    }
#endif
}

#endif
