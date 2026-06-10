/*
 * frank-micro — BBC Micro for RP2350
 * micro_keys.c — Programmatic key injection into the BBC keyboard matrix.
 *
 * See micro_keys.h.  Runs entirely on Core 0 in the BBC CPU thread context
 * (called from micro_keyboard_poll → micro_frame_present at 50 Hz), so the
 * beebjit keyboard_system_key_* calls are thread-safe here.
 */
#include "micro_keys.h"
#include "keyboard.h"
#include <string.h>

/* ── ASCII → BBC keyboard mapping ─────────────────────────────────────────
 * beebjit uses positional key codes (the PC physical key), with uppercase
 * letters as the code and SHIFT applied separately.  The symbol layout below
 * follows the BBC Micro keyboard (shifted symbols differ from a PC). */
struct key_map { uint8_t key; uint8_t shift; };

static struct key_map ascii_to_bbc(char c) {
    struct key_map m = { 0, 0 };

    if (c >= 'A' && c <= 'Z') { m.key = (uint8_t)c;            m.shift = 0; return m; }
    if (c >= 'a' && c <= 'z') { m.key = (uint8_t)(c - 'a' + 'A'); m.shift = 1; return m; }
    if (c >= '0' && c <= '9') { m.key = (uint8_t)c;            m.shift = 0; return m; }

    switch (c) {
    case ' ':  m.key = ' ';  m.shift = 0; break;
    case '.':  m.key = '.';  m.shift = 0; break;
    case ',':  m.key = ',';  m.shift = 0; break;
    case '/':  m.key = '/';  m.shift = 0; break;
    case ';':  m.key = ';';  m.shift = 0; break;
    case '-':  m.key = '-';  m.shift = 0; break;
    case ':':  m.key = '\''; m.shift = 0; break; /* BBC ':' is on PC apostrophe key */
    case '@':  m.key = '`';  m.shift = 0; break; /* BBC '@' is on PC backtick key */
    case '[':  m.key = '[';  m.shift = 0; break;
    case ']':  m.key = ']';  m.shift = 0; break;
    case '\\': m.key = '\\'; m.shift = 0; break;
    case '^':  m.key = '=';  m.shift = 0; break; /* BBC '^' is on PC '=' key */
    /* Shifted BBC symbols. */
    case '!':  m.key = '1';  m.shift = 1; break;
    case '"':  m.key = '2';  m.shift = 1; break;
    case '#':  m.key = '3';  m.shift = 1; break;
    case '$':  m.key = '4';  m.shift = 1; break;
    case '%':  m.key = '5';  m.shift = 1; break;
    case '&':  m.key = '6';  m.shift = 1; break;
    case '\'': m.key = '7';  m.shift = 1; break;
    case '(':  m.key = '8';  m.shift = 1; break;
    case ')':  m.key = '9';  m.shift = 1; break;
    case '*':  m.key = '\''; m.shift = 1; break; /* SHIFT+: → * */
    case '+':  m.key = ';';  m.shift = 1; break;
    case '=':  m.key = '-';  m.shift = 1; break;
    case '<':  m.key = ',';  m.shift = 1; break;
    case '>':  m.key = '.';  m.shift = 1; break;
    case '?':  m.key = '/';  m.shift = 1; break;
    default:   m.key = 0;    m.shift = 0; break; /* unsupported → ignore */
    }
    return m;
}

/* ── Key queue ────────────────────────────────────────────────────────────── */
#define KEYQ_SIZE 512
#define HOLD_FRAMES_DEFAULT 3   /* ~60 ms hold */
#define GAP_FRAMES          2   /* ~40 ms gap  */

struct keyq_entry { uint8_t key; uint8_t shift; uint8_t hold; };

static struct keyq_entry s_q[KEYQ_SIZE];
static volatile int s_head = 0;
static volatile int s_tail = 0;

/* State machine: phase>0 = holding (countdown), phase<0 = gap (countup). */
static int     s_phase = 0;
static uint8_t s_cur_key = 0;
static uint8_t s_cur_shift = 0;

void micro_key_enqueue(uint8_t key, int shift, int hold_frames) {
    int next = (s_tail + 1) % KEYQ_SIZE;
    if (next == s_head) return;            /* full — drop */
    s_q[s_tail].key   = key;
    s_q[s_tail].shift = (uint8_t)(shift ? 1 : 0);
    s_q[s_tail].hold  = (uint8_t)(hold_frames > 0 ? hold_frames : HOLD_FRAMES_DEFAULT);
    s_tail = next;
}

int micro_key_type(const char* text) {
    int n = 0;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n' || *p == '\r') {
            micro_key_enqueue(k_keyboard_key_enter, 0, HOLD_FRAMES_DEFAULT);
        } else {
            struct key_map m = ascii_to_bbc(*p);
            if (m.key == 0) continue;
            micro_key_enqueue(m.key, m.shift, HOLD_FRAMES_DEFAULT);
        }
        n++;
    }
    return n;
}

void micro_key_return(void) {
    micro_key_enqueue(k_keyboard_key_enter, 0, HOLD_FRAMES_DEFAULT);
}

void micro_key_break(int with_shift) {
    /* BREAK is wired to F12 in beebjit (triggers a soft reset).  For
     * SHIFT+BREAK we hold the key longer so SHIFT is asserted in the matrix
     * while the boot ROM samples it. */
    micro_key_enqueue(k_keyboard_key_f12, with_shift ? 1 : 0, 10);
}

int micro_key_busy(void) {
    return (s_head != s_tail) || (s_phase != 0);
}

void micro_key_tick(struct keyboard_struct* p_kbd) {
    if (!p_kbd) return;

    if (s_phase > 0) {
        /* Holding the current key. */
        if (--s_phase == 0) {
            keyboard_system_key_released(p_kbd, s_cur_key);
            if (s_cur_shift)
                keyboard_system_key_released(p_kbd, k_keyboard_key_shift_left);
            s_phase = -GAP_FRAMES;
        }
        return;
    }
    if (s_phase < 0) {
        s_phase++;                          /* inter-key gap */
        return;
    }

    /* Idle — pop next key. */
    if (s_head == s_tail) return;
    struct keyq_entry e = s_q[s_head];
    s_head = (s_head + 1) % KEYQ_SIZE;

    s_cur_key   = e.key;
    s_cur_shift = e.shift;
    if (s_cur_shift)
        keyboard_system_key_pressed(p_kbd, k_keyboard_key_shift_left);
    keyboard_system_key_pressed(p_kbd, s_cur_key);
    s_phase = e.hold;
}
