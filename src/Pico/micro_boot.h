/*
 * frank-micro — BBC Micro for RP2350
 * micro_boot.h — Boot/welcome screen.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef MICRO_BOOT_H
#define MICRO_BOOT_H

#include <stdbool.h>
#include "ff.h"

void micro_boot_welcome(uint32_t timeout_ms);
void micro_boot_error(bool sd_ok, FRESULT sd_result);

#endif /* MICRO_BOOT_H */
