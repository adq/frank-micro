#ifndef KEYBOARD_HELPER_H
#define KEYBAORD_HELPER_H

#include <stdbool.h>
#include <stdint.h>

#include "tusb.h"

// look up new key in previous keys
inline bool find_key_in_report(hid_keyboard_report_t const *p_report, uint8_t keycode)
{
  for(uint8_t i = 0; i < 6; i++)
  {
    if (p_report->keycode[i] == keycode)  return true;
  }

  return false;
}

inline uint8_t keycode_to_ascii(uint8_t modifier, uint8_t keycode)
{
  return keycode > 128 ? 0 :
    hid_keycode_to_ascii_tbl [keycode][modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT) ? 1 : 0];
}

#endif