/*
 * frank-micro — BBC Micro for RP2350
 * micro_ui.c — Simple OSD menu (disk browser, settings).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "micro_ui.h"
#include "micro_loader.h"
#include "micro_settings.h"
#include "ui_draw.h"
#include "board_config.h"
#include <string.h>
#include <stdio.h>

#define FB_W  MICRO_FB_WIDTH

static int g_visible = 0;

void micro_ui_init(void)    { g_visible = 0; }
int  micro_ui_is_visible(void) { return g_visible; }
void micro_ui_show(void)    { g_visible = 1; micro_disk_rescan(); }
void micro_ui_hide(void)    { g_visible = 0; }
void micro_ui_toggle(void)  { if (g_visible) micro_ui_hide(); else micro_ui_show(); }

static int g_selected = 0;

void micro_ui_render(uint8_t* fb, int stride, int height) {
    if (!g_visible) return;

    /* Dark overlay */
    ui_fill_rect(fb, stride, 0, 0, FB_W, height, UI_COLOR_BLACK);

    ui_draw_string(fb, stride, 10, 4, "frank-micro — BBC Disk Browser", UI_COLOR_FG);
    ui_draw_string(fb, stride, 10, 14, g_micro_disk_dir, UI_COLOR_DIM);

    int max_rows = (height - 30) / UI_CHAR_H;
    int count    = g_micro_disk_entry_count;

    for (int i = 0; i < count && i < max_rows; i++) {
        micro_disk_entry_t* e = &g_micro_disk_entries[i];
        char line[80];
        snprintf(line, sizeof(line), "%s%s", e->is_dir ? "[" : " ", e->name);
        if (e->is_dir) { size_t l = strlen(line); if (l < sizeof(line)-1) { line[l]=']'; line[l+1]='\0'; } }

        uint8_t col = (i == g_selected) ? UI_COLOR_ACCENT_FG : (e->is_dir ? UI_COLOR_OK : UI_COLOR_FG);
        if (i == g_selected)
            ui_fill_rect(fb, stride, 8, 28 + i * UI_CHAR_H, FB_W - 16, UI_CHAR_H, UI_COLOR_ACCENT);
        ui_draw_string(fb, stride, 12, 28 + i * UI_CHAR_H, line, col);
    }

    ui_draw_string(fb, stride, 10, height - 12,
                   "Up/Down: navigate  Enter: insert drive A  Esc: close", UI_COLOR_DIM);
}

int micro_ui_handle_key(int ps2_keycode, int pressed) {
    if (!pressed) return 0;

    /* PS/2 scan codes (set 2): UP=0x75, DOWN=0x72, ENTER=0x5A, ESC=0x76 */
    switch (ps2_keycode) {
    case 0x76: /* ESC */
        micro_ui_hide();
        return 1;
    case 0x75: /* UP */
        if (g_selected > 0) g_selected--;
        return 1;
    case 0x72: /* DOWN */
        if (g_selected < g_micro_disk_entry_count - 1) g_selected++;
        return 1;
    case 0x5A: /* ENTER */
        if (g_selected >= 0 && g_selected < g_micro_disk_entry_count) {
            micro_disk_entry_t* e = &g_micro_disk_entries[g_selected];
            if (e->is_dir) {
                micro_disk_enter_subdir(e->name);
                g_selected = 0;
            } else {
                char path[MICRO_DISK_PATH_LEN];
                micro_disk_entry_path(g_selected, path, sizeof(path));
                micro_mount_disk(0, path);
                micro_ui_hide();
            }
        }
        return 1;
    default:
        return 0;
    }
}
