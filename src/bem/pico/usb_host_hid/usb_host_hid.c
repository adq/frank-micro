/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/util/queue.h"
#include "bsp/board.h"
#include "tusb.h"
#include "keyboard_helper.h"
#include "usb_host_hid.h"

//--------------------------------------------------------------------+
// USB Keyboard
//--------------------------------------------------------------------+
#if CFG_TUH_HID_KEYBOARD
static hid_keyboard_report_t prev_keyboard_report;
static hid_keyboard_report_t keyboard_report;

queue_t kb_queue;

void tuh_hid_keyboard_mounted_cb(uint8_t dev_addr)
{
    // application set-up
    printf("\na Keyboard device (address %d) is mounted\n", dev_addr);
    tuh_hid_keyboard_get_report(dev_addr, &keyboard_report);
}

void tuh_hid_keyboard_unmounted_cb(uint8_t dev_addr)
{
    // application tear-down
    printf("\na Keyboard device (address %d) is unmounted\n", dev_addr);
}

static void push_key_event(bool down, int scancode) {
    uint32_t val = (down << 16) | scancode;
    queue_try_add(&kb_queue, &val);
}

void tuh_hid_keyboard_isr(uint8_t dev_addr, xfer_result_t event)
{
    if (event == XFER_RESULT_SUCCESS)
    {
        static uint8_t last_modifiers;
        uint8_t changed_modifiers = keyboard_report.modifier ^ last_modifiers;
        uint i=0;
        while (changed_modifiers && i < 8) {
            if (changed_modifiers & 1) {
                push_key_event(!(last_modifiers & 1), 224 + i);
            }
            changed_modifiers >>= 1;
            last_modifiers >>= 1;
            i++;
        }
        last_modifiers = keyboard_report.modifier;
        // I assume it's possible to have up to 6 keypress events per report?
        for(i = 0; i < 6; i++)
        {
            // Check for key presses
            if (keyboard_report.keycode[i]) {
                // If not in prev report then it is newly pressed
                if (!find_key_in_report(&prev_keyboard_report, keyboard_report.keycode[i])) {
                    push_key_event(true, keyboard_report.keycode[i]);
                }
            }
            // Check for key depresses (i.e. was present in prev report but not here)
            if (prev_keyboard_report.keycode[i]) {
                // If not present in the current report then depressed
                if (!find_key_in_report(&keyboard_report, prev_keyboard_report.keycode[i]))
                {
                    push_key_event(false, prev_keyboard_report.keycode[i]);
                }
            }
        }
    }

    // Copy current report to prev report
    prev_keyboard_report = keyboard_report;

    // Get next report
    tuh_hid_keyboard_get_report(dev_addr, &keyboard_report);
}

bool get_kb_event(struct kb_event *event) {
    uint32_t data = 0;
    if (queue_try_remove(&kb_queue, &data)) {
        event->down = data > 65535;
        event->scancode = (uint16_t)data;
        return true;
    }
    return false;
}
#endif


//--------------------------------------------------------------------+
// USB Mouse
//--------------------------------------------------------------------+
#if CFG_TUH_HID_MOUSE
static hid_mouse_report_t mouse_report;

void tuh_hid_mouse_mounted_cb(uint8_t dev_addr)
{
    // application set-up
    printf("\na Mouse device (address %d) is mounted\n", dev_addr);
    tuh_hid_mouse_get_report(dev_addr, &mouse_report);
}

void tuh_hid_mouse_unmounted_cb(uint8_t dev_addr)
{
    // application tear-down
    printf("\na Mouse device (address %d) is unmounted\n", dev_addr);
}

// invoked ISR context
void tuh_hid_mouse_isr(uint8_t dev_addr, xfer_result_t event)
{
    if (event == XFER_RESULT_SUCCESS)
    {
        printf("Mouse: x %d, y %d\n", mouse_report.x, mouse_report.y);
    }

    // Get next report
    tuh_hid_mouse_get_report(dev_addr, &mouse_report);
}
#endif

void usb_host_hid_init() {
#if CFG_TUH_HID_KEYBOARD
    queue_init(&kb_queue, sizeof(uint32_t), 4);
#endif
    tusb_init();
}

void usb_host_hid_poll() {
    tuh_task();
}
