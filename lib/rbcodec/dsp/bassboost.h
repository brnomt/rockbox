/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Bass booster - constant aggressive sub-bass thump
 * Fixed-point implementation for ARM targets (iPod Classic 6/7)
 *
 * Signal flow:
 *   Input → Dry path (full-band)
 *         → Low-shelf filter (boost, ~80-100Hz) → Soft-clip → Wet path
 *       → Mix dry + wet → Output gain → Output
 *
 * Two-path architecture: the low-shelf path provides always-on, aggressive
 * sub-bass with a cubic soft-clipper for density. The dry path retains
 * mids/highs clarity. No ducking, no upward expander behavior.
 *
 * Copyright (C) 2024
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
#ifndef BASSBOOST_H
#define BASSBOOST_H

#include <stdbool.h>

struct bassboost_settings
{
    bool enabled;
    int crossover_hz;       /* Hz, low-pass cutoff (default 80) */
    int sub_bass_gain;      /* 0-240, 0.1 dB fixed bass boost (default 120 = +12 dB) */
    int drive;              /* 0-100 saturation amount (default 0) */
    int mix;                /* 0-100% (default 100) */
    int output_gain;         /* -120 to +120, 0.1 dB (default 0) */
};

void dsp_set_bassboost(const struct bassboost_settings *settings);

#endif
