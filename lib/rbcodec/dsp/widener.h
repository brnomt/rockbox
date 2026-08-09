/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Stereo widener - mid/side width control with tamed low end
 *
 * Signal flow:
 *   L,R -> M/S decode -> Butterworth LP on the side signal
 *         -> side below crossover scaled by min(width, 100%)
 *         -> side above crossover scaled by width
 *         -> M/S encode -> Soft limiter
 *
 * Width 100% passes the signal through unchanged; above 100% only the
 * side content above the crossover is widened, so the bass stays as
 * mono-compatible as at 100%.
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
#ifndef WIDENER_H
#define WIDENER_H

#include <stdbool.h>

struct widener_settings
{
    bool enabled;

    int width;               /* 0-200% side signal width (default 100) */
    int crossover_hz;        /* Hz, bass mono crossover (default 150) */
};

void dsp_set_widener(const struct widener_settings *settings);

#endif
