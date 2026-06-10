/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Time-based automatic brightness controller.
 *
 * Two configurable schedule slots ("day" and "night"). When enabled, the
 * slot whose trigger time is the most recent in the past sets the current
 * backlight brightness; a single self-rearming kernel timeout fires at the
 * next slot transition. Between fires there is no per-minute polling and
 * no extra thread, so the idle cost is effectively zero.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#ifndef _TIMED_BRIGHTNESS_H
#define _TIMED_BRIGHTNESS_H

#include "config.h"

#if defined(HAVE_BACKLIGHT_BRIGHTNESS) && defined(CONFIG_RTC) && (CONFIG_RTC != 0)

#include <stdbool.h>

/* Re-evaluate the current rule and (re)arm the next timeout. Idempotent:
 * safe to call whenever any related setting changes, or after settings
 * are loaded from disk. */
void timed_brightness_apply(void);

/* Adapter callbacks matching the signatures expected by BOOL/INT settings'
 * option_callback fields, so changing any slot in the menu re-arms
 * immediately. */
void timed_brightness_cb_bool(bool v);
void timed_brightness_cb_int(int v);

#endif /* HAVE_BACKLIGHT_BRIGHTNESS && CONFIG_RTC */
#endif /* _TIMED_BRIGHTNESS_H */
