/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Time-based automatic brightness controller. See timed_brightness.h for
 * the design notes.
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
#include "config.h"

#if defined(HAVE_BACKLIGHT_BRIGHTNESS) && defined(CONFIG_RTC) && (CONFIG_RTC != 0)

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "timed_brightness.h"
#include "settings.h"
#include "backlight.h"
#include "kernel.h"
#include "timeout.h"
#include "timefuncs.h"
#include "screens.h"
#include "lang.h"
#include "talk.h"

#define MIN_PER_DAY  (24 * 60)
#define SLOT_DAY     0
#define SLOT_NIGHT   1

static struct timeout tb_tmo;
static bool tb_registered = false;

static inline int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int slot_minute_of(int hour, int min)
{
    return clamp_int(hour, 0, 23) * 60 + clamp_int(min, 0, 59);
}

static int slot_min_of_day(int slot)
{
    if (slot == SLOT_DAY)
        return slot_minute_of(global_settings.timed_brightness_day_hour,
                              global_settings.timed_brightness_day_min);
    return slot_minute_of(global_settings.timed_brightness_night_hour,
                          global_settings.timed_brightness_night_min);
}

static int slot_level(int slot)
{
    int level = (slot == SLOT_DAY)
        ? global_settings.timed_brightness_day_level
        : global_settings.timed_brightness_night_level;
    /* backlight_set_brightness clamps too, but clamping here protects the
     * config-file path if a user hand-edited bogus values. */
    return clamp_int(level, MIN_BRIGHTNESS_SETTING, MAX_BRIGHTNESS_SETTING);
}

/* Selects which slot is "currently active" -- i.e. whose trigger time is
 * the most recent past trigger in the 24h cycle -- and reports the slot
 * that will fire next plus the minutes until it does (1..MIN_PER_DAY). */
static int pick_current_slot(int now_min, int *next_out, int *mins_to_next_out)
{
    int day_m   = slot_min_of_day(SLOT_DAY);
    int night_m = slot_min_of_day(SLOT_NIGHT);

    int since_day   = (now_min - day_m   + MIN_PER_DAY) % MIN_PER_DAY;
    int since_night = (now_min - night_m + MIN_PER_DAY) % MIN_PER_DAY;

    int current = (since_day <= since_night) ? SLOT_DAY : SLOT_NIGHT;
    int next    = (current == SLOT_DAY) ? SLOT_NIGHT : SLOT_DAY;

    int mins = (slot_min_of_day(next) - now_min + MIN_PER_DAY) % MIN_PER_DAY;
    /* Wrap 0 to a full day so the timeout never has zero ticks (which
     * would mean "cancel"). This also covers the degenerate case where
     * both slots share the same minute-of-day. */
    if (mins == 0)
        mins = MIN_PER_DAY;

    *next_out = next;
    *mins_to_next_out = mins;
    return current;
}

/* ISR-context callback. Applies the brightness for the slot stored in
 * tmo->data and rearms itself for the other slot. We do not call
 * get_time() here -- everything we need is precomputed from the schedule,
 * so this stays interrupt-safe. backlight_set_brightness only does a
 * queue_post, which is explicitly ISR-safe. */
static int tb_callback(struct timeout *tmo)
{
    int fired = (int)tmo->data;
    int other = (fired == SLOT_DAY) ? SLOT_NIGHT : SLOT_DAY;

    backlight_set_brightness(slot_level(fired));

    int mins = (slot_min_of_day(other) - slot_min_of_day(fired)
                + MIN_PER_DAY) % MIN_PER_DAY;
    if (mins == 0)
        mins = MIN_PER_DAY;

    tmo->data = (intptr_t)other;
    return mins * 60 * HZ;
}

void timed_brightness_apply(void)
{
    if (!global_settings.timed_brightness_enabled)
    {
        if (tb_registered)
        {
            timeout_cancel(&tb_tmo);
            tb_registered = false;
        }
        backlight_set_brightness(global_settings.brightness);
        return;
    }

    struct tm *now = get_time();
    if (now == NULL || !valid_time(now))
    {
        /* RTC not ready: fall back to the user's manual brightness;
         * settings_apply() (or any later setting change) will retry. */
        backlight_set_brightness(global_settings.brightness);
        return;
    }

    int now_min = now->tm_hour * 60 + now->tm_min;
    int next, mins_to_next;
    int current = pick_current_slot(now_min, &next, &mins_to_next);

    backlight_set_brightness(slot_level(current));

    /* timeout_register accepts re-registration: it just resets expires. */
    timeout_register(&tb_tmo, tb_callback,
                     mins_to_next * 60 * HZ, (intptr_t)next);
    tb_registered = true;
}

void timed_brightness_cb_bool(bool v)
{
    (void)v;
    timed_brightness_apply();
}

void timed_brightness_cb_int(int v)
{
    (void)v;
    timed_brightness_apply();
}

static int timed_brightness_set_slot_time(bool night)
{
    struct tm atm;
    memset(&atm, 0, sizeof(atm));

    if (night)
    {
        atm.tm_hour = global_settings.timed_brightness_night_hour;
        atm.tm_min  = global_settings.timed_brightness_night_min;
    }
    else
    {
        atm.tm_hour = global_settings.timed_brightness_day_hour;
        atm.tm_min  = global_settings.timed_brightness_day_min;
    }
    atm.tm_sec = 0;

    int lang_id = night ? LANG_TIMED_BRIGHTNESS_NIGHT_TIME
                        : LANG_TIMED_BRIGHTNESS_DAY_TIME;
    bool usb = set_time_screen(str(lang_id), &atm, false);

    if (!usb && atm.tm_year != -1)
    {
        if (night)
        {
            global_settings.timed_brightness_night_hour = atm.tm_hour;
            global_settings.timed_brightness_night_min  = atm.tm_min;
        }
        else
        {
            global_settings.timed_brightness_day_hour = atm.tm_hour;
            global_settings.timed_brightness_day_min  = atm.tm_min;
        }
        timed_brightness_apply();
        settings_save();
    }

    return usb ? 1 : 0;
}

int timed_brightness_set_day_time(void)
{
    return timed_brightness_set_slot_time(false);
}

int timed_brightness_set_night_time(void)
{
    return timed_brightness_set_slot_time(true);
}

static char *format_slot_time(bool night, char *buffer, size_t buffer_len)
{
    int lang_id = night ? LANG_TIMED_BRIGHTNESS_NIGHT_TIME
                        : LANG_TIMED_BRIGHTNESS_DAY_TIME;
    int hour = night ? global_settings.timed_brightness_night_hour
                     : global_settings.timed_brightness_day_hour;
    int min  = night ? global_settings.timed_brightness_night_min
                     : global_settings.timed_brightness_day_min;

    snprintf(buffer, buffer_len, "%s (%02d:%02d)", str(lang_id), hour, min);
    return buffer;
}

char *timed_brightness_day_time_getname(int selected_item, void *data,
                                        char *buffer, size_t buffer_len)
{
    (void)selected_item;
    (void)data;
    return format_slot_time(false, buffer, buffer_len);
}

char *timed_brightness_night_time_getname(int selected_item, void *data,
                                          char *buffer, size_t buffer_len)
{
    (void)selected_item;
    (void)data;
    return format_slot_time(true, buffer, buffer_len);
}

static int speak_slot_time(bool night)
{
    int hour = night ? global_settings.timed_brightness_night_hour
                     : global_settings.timed_brightness_day_hour;
    int min  = night ? global_settings.timed_brightness_night_min
                     : global_settings.timed_brightness_day_min;
    int lang_id = night ? LANG_TIMED_BRIGHTNESS_NIGHT_TIME
                        : LANG_TIMED_BRIGHTNESS_DAY_TIME;

    talk_id(lang_id, true);
    talk_value(hour, UNIT_HOUR, true);
    talk_value(min, UNIT_MIN, true);
    return 0;
}

int timed_brightness_day_time_speak(int selected_item, void *data)
{
    (void)selected_item;
    (void)data;
    return speak_slot_time(false);
}

int timed_brightness_night_time_speak(int selected_item, void *data)
{
    (void)selected_item;
    (void)data;
    return speak_slot_time(true);
}

#endif /* HAVE_BACKLIGHT_BRIGHTNESS && CONFIG_RTC */
