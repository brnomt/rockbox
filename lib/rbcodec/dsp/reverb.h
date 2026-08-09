/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Mini reverb - compact Schroeder/freeverb style room simulator
 *
 * Signal flow:
 *   (L+R)/2 -> 2x4 parallel comb filters with damped feedback
 *            -> one set per stereo channel (detuned delay lengths)
 *            -> wet/dry mix -> Soft limiter
 *
 * Copyright (C) 2026
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
#ifndef REVERB_H
#define REVERB_H

#include <stdbool.h>

struct reverb_settings
{
    bool enabled;

    int room_size;           /* 0-100, delay lengths + feedback (default 50) */
    int damping;             /* 0-100, treble loss in the tail (default 50) */
    int wet_mix;             /* 0-100% wet/dry mix (default 30) */
};

void dsp_set_reverb(const struct reverb_settings *settings);

#endif
