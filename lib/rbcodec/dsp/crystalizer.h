/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Crystalizer - multiband transient/peak enhancer
 *
 * Inspired by EasyEffects Crystalizer.
 * Algorithm: split signal into frequency bands via LR2 crossover network.
 * For each band, compute the backward-difference second derivative and add
 * it scaled by intensity. This sharpens transients and adds "crystal"
 * clarity to the audio.
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
#ifndef CRYSTALIZER_H
#define CRYSTALIZER_H

#include <stdbool.h>

struct crystalizer_settings
{
    bool enabled;
    int intensity_mid;       /* -240 to +240, 0.1 dB (default 0) */
    int intensity_high;      /* -240 to +240, 0.1 dB (default 0) */
    int output_gain;         /* -120 to +120, 0.1 dB (default 0) */
    int mix;                 /* 0-100% (default 100) */
};

void dsp_set_crystalizer(const struct crystalizer_settings *settings);

#endif
