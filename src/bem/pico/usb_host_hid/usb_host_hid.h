/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#ifndef _USB_HOST_HID
#define _USB_HOST_HID

#ifdef __cplusplus
extern "C" {
#endif

void usb_host_hid_init();
void usb_host_hid_poll();

struct kb_event {
    uint16_t scancode;
    bool down;
};

bool get_kb_event(struct kb_event *event);

#ifdef __cplusplus
}
#endif
#endif
