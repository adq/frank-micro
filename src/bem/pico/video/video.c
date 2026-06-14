/* B-em v2.2 by Tom Walker
 * Pico version (C) 2021 Graham Sanderson
 * Video emulation
 * Incorporates 6845 CRTC, Video ULA and SAA5050*/

#include "b-em.h"

#include "mem.h"
#include "model.h"
#include "serial.h"
#include "via.h"
#include "sysvia.h"
#include "video.h"
#include "video_render.h"
#include "display.h"
#include "hardware/gpio.h"

CU_REGISTER_DEBUG_PINS(hcycles)
//CU_SELECT_DEBUG_PINS(hcycles);

// todo gonna want to struct-ify some of this state

// todo note that LOOK_MA_NO_MA seemed like a good idea at the time (and still is, and is currently used),
//  but address can perhaps be re-calculated once per video_run_cycles from ma (only reason to do so is
//  ma makes it easier to reason about CRTC bug fixes in the future perhaps)

static int interlline = 0;
static int scrsize;

// can probably be more than this
#define CRTC0_MIN 4

#ifdef USE_HW_EVENT
// keep a priority queue of HC events; we want this!
#define USE_HC_PRIORITY
static bool _shadow;
#endif

#ifdef USE_HC_PRIORITY
// return true if the priority may have changed
typedef void (*hc_priority_fn)();
struct hc_priority {
    hc_priority_fn fn;
    uint8_t tie_breaker;
    bool after_display_char;
    uint8_t pos;
    struct hc_priority *next;
};
#endif

static struct {
#ifndef USE_HC_PRIORITY
    uint16_t h_displayed; // crtc[1]
    uint16_t h_sync_pos; // crtc[2]
    uint16_t h_half_line_end; // interline && hc == (crtc[0] >> 1)
    uint16_t h_line_end; // crtc[0]
#else
    struct hc_priority *h_priorities;
#endif
    const uint8_t *ram_base;
    uint32_t mem_addr;
    uint32_t mem_addr_save;
    uint32_t mem_addr_delta;
    uint32_t con_addr;
    uint32_t screen_size;
    uint32_t display_addr_latched;
    uint8_t hc;
    bool dispen;
    bool hvblcount;
    bool interline;
} hc_state;

void reached_h_displayed();
void reached_h_sync_pos();
void reached_h_half_line_end();
void reached_h_line_end();
void reached_h_wrap();

#ifdef USE_HC_PRIORITY
static struct hc_priority h_displayed_priority = {
        .fn = reached_h_displayed,
        .tie_breaker = 0,
        .after_display_char = false
};
static struct hc_priority h_sync_pos_priority = {
        .fn = reached_h_sync_pos,
        .tie_breaker = 1,
        .after_display_char = false
};
static struct hc_priority h_half_line_end_priority = {
        .fn = reached_h_half_line_end,
        .tie_breaker = 2,
        .after_display_char = true
};
static struct hc_priority h_line_end_priority = {
        .fn = reached_h_line_end,
        .tie_breaker = 3,
        .after_display_char = true
};
static struct hc_priority h_wrap = {
        .fn = reached_h_wrap,
        .tie_breaker = 4,
        .after_display_char = true,
        .pos = 0xff
};
#endif


/*Video ULA (VIDPROC)*/

/*6845 CRTC*/
// todo there aren't 32 regs!
static uint8_t crtc[32];
static uint8_t crtc_mask[32] = {0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x1F, 0x7F, 0x7F, 0xF3, 0x1F, 0x7F, 0x1F, 0x3F, 0xFF,
                                0x3F, 0xFF, 0x3F, 0xFF};

static int crtc_i;

static int vc, sc;
static int vadj;
#ifndef LOOK_MA_NO_MA
static uint16_t ma;
static uint16_t maback;
static const int cdrawlook[4] = {3, 2, 1, 0};
static uint16_t vidbank;
#endif
static int vdispen;

static const int cmask[4] = {0, 0, 16, 32};

static bool low_freq;
static uint8_t ula_ctrl;

static const int screenlen[4] = {0x4000, 0x5000, 0x2000, 0x2800};

static int8_t vsynctime;
//int8_t interline;
static int8_t frameodd;
static bool con, coff;
int8_t cursoron;
static int frcount;

int vidclocks = 0;
static bool oddclock = 0;

#ifdef LOOK_MA_NO_MA

void init_mem_addr() {
    uint ma = hc_state.display_addr_latched = (crtc[CRTC_DISPLAY_ADDR_L] | (crtc[CRTC_DISPLAY_ADDR_H] << 8)) & 0x3FFF;
    // note we don't correct handle a mid-line transition between ROM as video RAM/teletext addressing; awwww shucks!
    uint32_t addr;
    if (ma & 0x2000) {
        addr = 0x7C00 | (ma & 0x3FF);
        hc_state.mem_addr_delta = 1;
        hc_state.screen_size = 0x400;
    } else {
        addr = ma << 3;

        if (addr & 0x8000)
            addr -= screenlen[scrsize];

        hc_state.mem_addr_delta = 8;
        hc_state.screen_size = screenlen[scrsize];
    }
    hc_state.mem_addr = hc_state.mem_addr_save = addr;
}

#endif

#ifdef USE_HC_PRIORITY

// note anything comes before NULL
static bool hc_priority_before(struct hc_priority *x, struct hc_priority *y) {
    if (!y || x->pos < y->pos || (x->pos == y->pos && x->tie_breaker < y->tie_breaker)) return true;
    assert(x->tie_breaker != y->tie_breaker); // we shouldn't have duplicates
    return false;
}

static inline void add_hc_priority(struct hc_priority *prio) {
    struct hc_priority *prev = hc_state.h_priorities;
    if (hc_priority_before(prio, prev)) {
        hc_state.h_priorities = prio;
        prio->next = prev;
    } else {
        assert(prev);
        while (!hc_priority_before(prio, prev->next)) {
            prev = prev->next;
        }
        assert(prev);
        prio->next = prev->next;
        prev->next = prio;
    }
}

#endif

static void update_hc_limits() {
//    printf("Update hc\n");
#ifndef USE_HC_PRIORITY
    hc_state.h_displayed = crtc[CRTC_HDISPLAY];
    hc_state.h_sync_pos = crtc[CRTC_HSYNC_POS];
    hc_state.h_half_line_end = hc_state.interline ? (crtc[CRTC_HTOTAL] >> 1) : 0x100;
    hc_state.h_line_end = crtc[CRTC_HTOTAL];
#else
    // we add in what we expect to be reverse order (in the usual case) - it doesn't need to be reverse tho
    if (!hc_state.interline) {
        h_line_end_priority.pos = crtc[CRTC_HTOTAL];
        hc_state.h_priorities = &h_line_end_priority;
        h_line_end_priority.next = &h_wrap;
    } else {
        h_half_line_end_priority.pos = crtc[CRTC_HTOTAL] >> 1;
        hc_state.h_priorities = &h_half_line_end_priority;
        h_half_line_end_priority.next = &h_wrap;
    }
    h_sync_pos_priority.pos = crtc[CRTC_HSYNC_POS];
    add_hc_priority(&h_sync_pos_priority);
    h_displayed_priority.pos = crtc[CRTC_HDISPLAY];
    add_hc_priority(&h_displayed_priority);
#if 0
    struct hc_priority *prio = hc_state.h_priorities;
    while (prio) {
        printf("%d:%d ", prio->pos, prio->tie_breaker);
        prio = prio->next;
    }
    printf("\n");
#endif
#endif
}

void set_scrsize(int s) {
    if (s != scrsize) {
        // todo revisit this - why does this break smooth scroll and off set wave runninger scrolling text 1 char to right
        //  related to 12/13 latching?
//        video_cycle_sync();
        scrsize = s;
#ifdef LOOK_MA_NO_MA
        if (!(hc_state.display_addr_latched & 0x2000))
            hc_state.screen_size = screenlen[scrsize];
#endif
    }
}

#if PIXELATED_PAUSE
bool clear_pixelated_pause;

void set_pixelated_pause(bool pause) {
    if (pause && !cpu_pps) {
        cpu_pps = PPS_SETUP_FRAME;
    } else if (!pause) {
        clear_pixelated_pause = true;
    }
}
#endif

#ifndef DISPLAY_WIRE
#define record_crtc_reset effect_crtc_reset
#define record_display_reset effect_display_reset
#define record_row_start effect_row_start
#define record_row_end effect_row_end
#define record_hsync_pos effect_hsync_pos
#define record_vsync_pos effect_vsync_pos
#define record_displayed_chars effect_displayed_chars
#define record_non_displayed_char effect_non_displayed_char
#define record_crtc_write effect_crtc_write
#define record_ula_write effect_ula_write
#define record_hdisplay_count_pos effect_hdisplay_count_pos
#define record_cdraw effect_cdraw
#else

static struct scanvideo_scanline_buffer *current_record_buffer;


static inline void record_word(uint rec, uint val) {
    assert(current_record_buffer);
    if (current_record_buffer->data_used >= current_record_buffer->data_max - 1) {
        squash_record_buffer(current_record_buffer);
    }
    assert(current_record_buffer->data_used < current_record_buffer->data_max);
    assert(rec <= 0xffff);
    assert(val <= 0xffff);
    current_record_buffer->data[current_record_buffer->data_used++] = (rec << 16u) | val;
}

void sound_record_word(uint rec, uint val) {
    record_word(rec, val);
}

static inline uint32_t *get_record_buffer_window(uint words) {
    assert(current_record_buffer);
    if (current_record_buffer->data_used + words >= current_record_buffer->data_max) {
        squash_record_buffer(current_record_buffer);
    }
    assert(current_record_buffer->data_used + words <= current_record_buffer->data_max);
    return current_record_buffer->data + current_record_buffer->data_used;
}

static inline void submit_record_buffer_window(uint words) {
    current_record_buffer->data_used += words;
    assert(current_record_buffer->data_used <= current_record_buffer->data_max);
}

// must be called first
static inline void record_crtc_reset() {
    current_record_buffer = commit_record_buffer(current_record_buffer, true);
}

static inline void record_display_reset() {
    record_word(REC_DISPLAY_RESET, 0);
}

static inline void record_crtc_write(uint reg, uint value) {
    record_word(REC_CRTC_REG, (reg << 8u) | value);
}
static inline void record_ula_write(int reg, int value) {
    record_word(REC_ULA_REG, (reg << 8u) | value);
}

// todo i think these are all boolean (well vadj > 0)
static inline void record_row_start(int vdispen, int vadj, int interline, int interlline, int cursoron, int sc) {
    // commit line
    current_record_buffer = commit_record_buffer(current_record_buffer, false);
    struct row_start_params p;
    p.vdispen = vdispen;
    p.vadj = vadj != 0;
    p.interlline = interlline;
    p.interline = interline;
    p.cursoron = cursoron?1:0;
    p.sc = sc;
    record_word(REC_ROW_START, p.val);
}

static inline void record_row_end(int hc, bool full_line) {
    record_word(full_line ? REC_ROW_END_FULL : REC_ROW_END_HALF, hc);
}

static inline void record_hdisplay_count_pos(bool dispen) {
    record_word( REC_HDISPLAY_COUNT_POS, dispen);
}
static inline void record_hsync_pos() {
    record_word( REC_HSYNC_POS, 0);
}

// annoying but updated independently of row_start right now
static inline void record_vsync_pos(int interline, int interlline, int pixelated_pause) {
    record_word( REC_VSYNC_POS, (pixelated_pause << 15) | (interline << 8) | interlline);
}

static inline void record_cdraw(int cdraw) {
    record_word( REC_CDRAW, cdraw);
}

#endif

void crtc_reset() {
    memset(&hc_state, 0, sizeof(hc_state));
#ifdef USE_HC_PRIORITY
    hc_state.h_priorities = &h_wrap;
#endif
    vc = sc = vadj = 0;
    record_crtc_reset();
    crtc[CRTC_CHAR_HEIGHT] = 10;
    low_freq = true;
}

static void video_run_cycles(int);

#ifdef USE_HW_EVENT

static bool __time_critical_func(htotal_invoke)(struct hw_event *event) {
    video_cycle_sync();
    return false;
}

static struct hw_event htotal_event = {
        .invoke = htotal_invoke
};

void __time_critical_func(video_cycle_sync)() {
    DEBUG_PINS_SET(hcycles, 4);
    int32_t delta = get_hardware_timestamp() - htotal_event.user_time;
    assert(delta >= 0);
#ifdef USE_CRTC0_0_OPTIMIZATION
    if (delta > 1024) { // smooth7 seems to rely on 475 - not sure why, but fine.
        delta = 1024;
    }
#endif
    // note it is important to call this even if delta == 0 as
    // USE_CRTC0_0_OPTIMIZATION uses it to resync events when crt0[0] is changed from 0 -> X
    video_run_cycles(delta);
    DEBUG_PINS_CLR(hcycles, 4);
}

#else
static void video_cycle_sync() {}
#endif

void __time_critical_func(crtc_write)(uint16_t addr, uint8_t val) {
//        log_debug("Write CRTC %04X %02X %04X\n",addr,val,pc);
    if (!(addr & 1))
        crtc_i = val & 31;
    else {
#ifdef USE_HW_EVENT
#ifdef USE_CRTC0_0_OPTIMIZATION
        if (!crtc_i) {
            if (val >= CRTC0_MIN && crtc[CRTC_HTOTAL] < CRTC0_MIN) {
                // todo check this -1 now?
                htotal_event.user_time = get_hardware_timestamp();// - 1; // note -1 so run cycles does something and adds event
            }
        }
#endif
#endif
        video_cycle_sync();
        val &= crtc_mask[crtc_i];
        crtc[crtc_i] = val;
        record_crtc_write(crtc_i, val);
        if (crtc_i == 6 && vc == val) {
            vdispen = 0;
        }
        if (crtc_i < 3) update_hc_limits();
    }
}

uint8_t __time_critical_func(crtc_read)(uint16_t addr) {
    if (!(addr & 1))
        return crtc_i;
    // todo note no need to sync clocks here for now.. the only thing that updates
    //  it based on internal state is reset or light pen (which we don't care aobut yet).
    return crtc[crtc_i];
}

void crtc_latchpen() {
    // todo; i got rid of ma because latchpen is the only thing that needs it.
    //  should be able to figure it out from state.mem_addr
//    crtc[CRTC_LIGHTPEN_ADDR_H] = (ma >> 8) & 0x3F;
//    crtc[CRTC_LIGHTPEN_ADDR_L] = ma & 0xFF;
}

#ifndef NO_USE_SAVE_STATE
void crtc_savestate(FILE * f)
{
    int c;
    for (c = 0; c < 18; c++)
        putc(crtc[c], f);
    putc(vc, f);
    putc(sc, f);
    putc(hc, f);
    putc(ma, f);
    putc(ma >> 8, f);
    putc(maback, f);
    putc(maback >> 8, f);
}

void crtc_loadstate(FILE * f)
{
    int c;
    for (c = 0; c < 18; c++)
        crtc[c] = getc(f);
    vc = getc(f);
    sc = sc_mode7 = getc(f);
    hc = getc(f);
    ma = getc(f);
    ma |= getc(f) << 8;
    maback = getc(f);
    maback |= getc(f) << 8;
}
#endif


void video_set_disptype(enum vid_disptype dtype) {
    vid_dtype_user = dtype;
    set_intern_dtype(dtype);
}

void video_reset() {
//    hc.interline = 0;
    vsynctime = 0;
    frameodd = 0;
    con = 0;
    cursoron = 0;
    hc_state.ram_base = ram;
    record_display_reset();
}

void video_poll(int clocks, int timer_enable) {
    assert(timer_enable);
    video_run_cycles(clocks);
}

#ifdef USE_HC_PRIORITY

static struct hc_priority *find_prio() {
    struct hc_priority *prio = hc_state.h_priorities;
    while (prio->pos < hc_state.hc) {
        prio = prio->next;
    }
//    if (prio != &h_dummy) {
//        printf("Ah ha\n");
//    }
    assert(prio);
    return prio;
}

#endif

static inline void pixel_guts(int len) {

#ifndef LOOK_MA_NO_MA
    if (con && !((ma ^ (crtc[CRTC_CURSOR_ADDR_L] | (crtc[CRTC_CURSOR_ADDR_H] << 8))) & 0x3FFF))
            record_cdraw(cdrawlook[crtc[CRTC_MODE_CTRL] >> 6]);
        uint16_t addr;
        uint8_t dat;
        if (ma & 0x2000)
            dat = hc_state.ram_base[0x7C00 | (ma & 0x3FF)];
        else {
            if ((crtc[CRTC_MODE_CTRL] & 3) == 3)
                addr = (ma << 3) | ((sc & 3) << 1) | interlline;
            else
                addr = (ma << 3) | (sc & 7);
            if (addr & 0x8000)
                addr -= screenlen[scrsize];
            dat = hc_state.ram_base[(addr & 0x7FFF) | vidbank];
        }
        record_displayed_char(dat);
        ma++;
#else
    if (!len) return;
    assert(len <= 255);
#ifdef DISPLAY_WIRE
    uint word_count = 1 + (len + 3) / 4;
    uint32_t *data = get_record_buffer_window(word_count);
    data[0] = (REC_DRAW_BYTES << 16) | len;
    uint8_t *bytes = (uint8_t *)&data[1];
#else
    static uint8_t bytes[256];
    uint cdraw_pos = 0;
#endif
    uint end = 0x8000u;
    if (cursoron && hc_state.con_addr < end) end = hc_state.con_addr;
    if (hc_state.mem_addr + len * hc_state.mem_addr_delta < end) {
        // faster loop (most common)
        for (uint i = 0; i < len; i++) {
            bytes[i] = hc_state.ram_base[hc_state.mem_addr];
            hc_state.mem_addr += hc_state.mem_addr_delta;
        }
    } else {
        for (uint i = 0; i < len; i++) {
#ifdef DISPLAY_WIRE
            if (hc_state.mem_addr == hc_state.con_addr) {
                data[0] |= ((i + 1) << 8);
            }
#else
            if (hc_state.mem_addr == hc_state.con_addr) {
                cdraw_pos = i + 1;
            }
#endif
            bytes[i] = hc_state.ram_base[hc_state.mem_addr];
            hc_state.mem_addr += hc_state.mem_addr_delta;
            if (hc_state.mem_addr & 0x8000u) {
                hc_state.mem_addr -= hc_state.screen_size;
            }
        }
    }
#ifdef DISPLAY_WIRE
    submit_record_buffer_window(word_count);
#else
    record_displayed_chars(bytes, len, cdraw_pos);
#endif
#endif

}

#ifndef USE_HC_PRIORITY
static void __time_critical_func(video_clock_loop)(int clocks) {
    while (clocks--) {
        oddclock = !oddclock;
        if (low_freq && !oddclock)
            continue;

        // A - this seems to just be to delay by 1 cycle
        if (hc_state.hvblcount) {
            sysvia_set_ca1(0);
            hc_state.hvblcount = 0;
        }

        if (hc_state.hc == hc_state.h_displayed) { // reached horizontal displayed count.
            reached_h_displayed();
        }

        if (hc_state.hc == hc_state.h_sync_pos) { // reached horizontal sync position.
            reached_h_sync_pos();
        }

        // ==== BEGIN pixel drawing ====
        if (hc_state.dispen) {
            pixel_guts(1);
        }
        // ==== END pixel drawing ====

        // ==== CHECK FOR END OF LINE ====
        if (hc_state.hc == hc_state.h_half_line_end) {
//            if (reached_h_half_line_end()) {
//                update_hc_limits();
//            }
            reached_h_half_line_end();
            update_hc_limits();
        } else if (hc_state.hc == hc_state.h_line_end) {
//            if (reached_h_line_end()) {
//                update_hc_limits();
//            }
            reached_h_line_end();
            update_hc_limits();

        } else {
            hc_state.hc++;
        }
    }
}
#else

static void __time_critical_func(video_clock_loop)(int clocks) {
    struct hc_priority *prio = find_prio();
    assert(clocks);
    assert(prio); // there is a dummy one on the end we can't be past
    if (low_freq) {
        if (oddclock) {
            oddclock = 0;
            clocks--;
            if (!clocks) return;
        }
        if (clocks & 1) {
            clocks++;
            oddclock = 1;
        }
        clocks >>= 1;
    }
    do {
        // A - this seems to just be to delay by 1 cycle
        if (hc_state.hvblcount) {
            sysvia_set_ca1(0);
            hc_state.hvblcount = 0;
        }

        // catch up
        assert(prio->pos >= hc_state.hc);
        uint len = prio->pos - hc_state.hc;
        len = MIN(len, clocks);
        if (hc_state.dispen) {
            pixel_guts(len);
        }

        while (!prio->after_display_char && hc_state.hc == prio->pos) {
            prio->fn();
            // this either hdisplay or hsyncpos neither of which change the list
            prio = prio->next;
        }

        clocks -= len;
        // ==== END pixel drawing ====

        // ==== CHECK FOR END OF LINE ====
        if (prio->after_display_char && hc_state.hc == prio->pos) {
            if (hc_state.dispen) {
                //printf("probably doesn't happen much\n");
                pixel_guts(1);
            }
            prio->fn();
            assert(hc_state.hc == 0);
            clocks--;
            prio = hc_state.h_priorities;
        } else {
            hc_state.hc += len;
        }
        assert(clocks >= 0);
    } while (clocks);
}
#endif

void __time_critical_func(video_run_cycles)(int clocks) {
    if (!clocks) return;
    video_clock_loop(clocks);

#ifdef USE_HW_EVENT
    // note 255 arbitrary for invalid htotal; we just want to wake up periodically
    uint hc_target = hc_state.interline ? (crtc[CRTC_HTOTAL] >> 1u) : crtc[CRTC_HTOTAL];
#ifdef USE_CRTC0_0_OPTIMIZATION
    int delay = crtc[CRTC_HTOTAL] >= CRTC0_MIN ? ((hc_target - hc_state.hc) & 0xffu) : 0x100;
#else
    uint delay = (hc_target - hc) & 0xffu;
#endif

    if (hc_state.hvblcount) delay = 1; // wake up next cycle
    if (low_freq) {
        delay *= 2;
        if (oddclock) delay++; // probably
    }
    delay++; // we are counting delay until the next cycle

    htotal_event.user_time = get_hardware_timestamp();
    cycle_timestamp_t t = htotal_event.user_time + delay;
//    if (t != htotal_event.target) {
    htotal_event.target = t;
    upsert_hw_event(&htotal_event);
//        printf("add vsync delayed %d for %d hc %d/%d vc %d\n", delay, t, hc, crtc[0], vc);
//    }
#endif
}

void reached_h_wrap() {
//    printf("%d: wrap %d\n", get_hardware_timestamp(), hc_state.hc);
    hc_state.hc = 0;
}

void reached_h_sync_pos() {
    record_hsync_pos();
}

void reached_h_displayed() {
//    printf("%d: hdisp %d\n", get_hardware_timestamp(), hc_state.hc);
    record_hdisplay_count_pos(hc_state.dispen);
    hc_state.dispen = 0;
}

void reached_h_half_line_end() {
//    printf("%d: hl %d\n", get_hardware_timestamp(), hc_state.hc);
    record_row_end(hc_state.hc, false);
    hc_state.hc = hc_state.interline = 0;
    record_row_start(vdispen, vadj, hc_state.interline, interlline, cursoron, sc);
    update_hc_limits();
}

void reached_h_line_end() {
//    printf("%d: le %d\n", get_hardware_timestamp(), hc_state.hc);
    record_row_end(hc_state.hc, true);

    hc_state.hc = 0;
    int interline = hc_state.interline;

    if (sc == (crtc[CRTC_CURSOR_END] & 31) ||
        ((crtc[CRTC_MODE_CTRL] & 3) == 3 && sc == ((crtc[CRTC_CURSOR_END] & 31) >> 1))) {
        con = 0;
        coff = 1;
    }

    if (vadj) {
        sc++;
        sc &= 31;
        vadj--;
        if (!vadj) {
            vdispen = 1;
#ifndef LOOK_MA_NO_MA
            ma = maback = (crtc[CRTC_DISPLAY_ADDR_L] | (crtc[CRTC_DISPLAY_ADDR_H] << 8)) & 0x3FFF;
#else
            init_mem_addr();
#endif
            sc = 0;
        } else {
#ifndef LOOK_MA_NO_MA
            ma = maback;
#else
            hc_state.mem_addr = hc_state.mem_addr_save;
#endif
        }
    } else {
        if (sc == crtc[CRTC_CHAR_HEIGHT] || ((crtc[CRTC_MODE_CTRL] & 3) == 3 && sc == (crtc[CRTC_CHAR_HEIGHT] >> 1))) {
            // Reached the bottom of a row of characters.
#ifndef LOOK_MA_NO_MA
            maback = ma;
#else
            hc_state.mem_addr_save = hc_state.mem_addr;
#endif
            sc = 0;
            con = 0;
            coff = 0;
            int oldvc = vc;
            vc++;
            vc &= 127;
            if (vc == crtc[CRTC_VDISPLAY]) // vertical displayed total.
                vdispen = 0;
            if (oldvc == crtc[CRTC_VTOTAL]) {
                // vertical total reached.
                vc = 0;
                vadj = crtc[CRTC_VTOTAL_ADJ];
                if (!vadj) {
                    vdispen = 1;
#ifndef LOOK_MA_NO_MA
                    ma = maback = (crtc[CRTC_DISPLAY_ADDR_L] | (crtc[CRTC_DISPLAY_ADDR_H] << 8)) & 0x3FFF;
#else
                    init_mem_addr();
#endif
                }
                frcount++;
                if (!(crtc[CRTC_CURSOR_START_CTRL] & 0x60))
                    cursoron = 1;
                else
                    cursoron = frcount & cmask[(crtc[CRTC_CURSOR_START_CTRL] & 0x60) >> 5];
            }
            if (vc == crtc[CRTC_VSYNC_POS]) {
                // Reached vertical sync position.
                int intsync = crtc[CRTC_MODE_CTRL] & 1;
                frameodd ^= 1;

                // seems like a bug in the original b-em... shouldn't set interline until the end of the vsync
                // ^^ seems like i was wrong, REVS needs this!!! whatever i needed to change it 4 before I no longer seem to need to!
                if (frameodd)
                    interline = intsync;

                interlline = frameodd && intsync;

                sysvia_set_ca1(1);

                vsynctime = (crtc[CRTC_SYNC_WIDTH] >> 4) + 1;
                if (!(crtc[CRTC_SYNC_WIDTH] >> 4))
                    vsynctime = 17;

                bool pixelated_pause = false;
#if PIXELATED_PAUSE
                switch (cpu_pps) {
                    case PPS_NONE:
                        clear_pixelated_pause = false;
                        break;
                    case PPS_SETUP_FRAME:
                        cpu_pps = PPS_ACTIVE;
                        pixelated_pause = true;
                        break;
                    case PPS_ACTIVE:
                        break;
                }
#endif
                record_vsync_pos(interline, interlline, pixelated_pause);
            }
        } else {
            sc++;
            sc &= 31;
#ifndef LOOK_MA_NO_MA
            ma = maback;
#else
            hc_state.mem_addr = hc_state.mem_addr_save;
#endif
        }
    }

    if ((sc == (crtc[CRTC_CURSOR_START_CTRL] & 31) ||
         ((crtc[CRTC_MODE_CTRL] & 3) == 3 && sc == ((crtc[CRTC_CURSOR_START_CTRL] & 31) >> 1))) && !coff)
        con = 1;

#ifdef LOOK_MA_NO_MA
    // need character row offset
    if (!(hc_state.display_addr_latched & 0x2000)) {
        hc_state.mem_addr &= ~7;
        if ((crtc[CRTC_MODE_CTRL] & 3) == 3)
            hc_state.mem_addr |= ((sc & 3) << 1) | interlline;
        else
            hc_state.mem_addr |= sc & 7;
    }

    if (con) {
        uint32_t ma = (crtc[CRTC_CURSOR_ADDR_L] | (crtc[CRTC_CURSOR_ADDR_H] << 8)) & 0x3FFF;
        uint32_t addr;
        // todo what if ma & 0x20 != display & c_addr
        if (hc_state.display_addr_latched & 0x2000) {
            addr = 0x7C00 | (ma & 0x3FF);
        } else {
            addr = ma << 3;
            if (addr & 0x8000) addr -= screenlen[scrsize];
        }
        hc_state.con_addr = addr | (hc_state.mem_addr & 7);
    } else {
        hc_state.con_addr = -1;
    }
#endif


    if (vsynctime) {
        vsynctime--;
        if (!vsynctime) {
            hc_state.hvblcount = 1;
            if (frameodd) {
                interline = (crtc[CRTC_MODE_CTRL] & 1);
            }
        }
    }

    hc_state.dispen = vdispen;
    record_row_start(vdispen, vadj, interline, interlline, cursoron, sc);
    // only need to update limits when interline changes (none of the above code changes crtc regs)
    if (hc_state.interline != interline) {
        hc_state.interline = interline;
        // the line end limit will have changed
        update_hc_limits();
    }
}

#ifndef NO_USE_SAVE_STATE
void video_savestate(FILE * f)
{
    putc(scrx, f);
    putc(scrx >> 8, f);
    putc(scry, f);
    putc(scry >> 8, f);
    putc(oddclock, f);
    putc(vidclocks, f);
    putc(vidclocks >> 8, f);
    putc(vidclocks >> 16, f);
    putc(vidclocks >> 24, f);
}

void video_loadstate(FILE * f)
{
    scrx = getc(f);
    scrx |= getc(f) << 8;
    scry = getc(f);
    scry |= getc(f) << 8;
    oddclock = getc(f);
    vidclocks = getc(f);
    vidclocks = getc(f) << 8;
    vidclocks = getc(f) << 16;
    vidclocks = getc(f) << 24;
}
#endif

void __time_critical_func(videoula_write)(uint16_t addr, uint8_t val) {
    if (nula_disable)
        addr &= ~2;             // nuke additional NULA addresses

    video_cycle_sync();
    int reg = addr & 3;
    if (!reg) {
        // Video control register.
        // log_debug("video: ULA write VCR from %04X: %02X %i %i\n",pc,val,hc,vc);

        ula_ctrl = val;
        low_freq = !(ula_ctrl & 0x10);
    }
    record_ula_write(reg, val);
}

void select_vidbank(bool shadow) {
#ifdef USE_HW_EVENT
    if (_shadow != shadow) {
        video_cycle_sync();
        _shadow = shadow;
    }
#endif
#ifdef LOOK_MA_NO_MA
    hc_state.ram_base = ram + (shadow ? 0x8000 : 0);
#endif
}

