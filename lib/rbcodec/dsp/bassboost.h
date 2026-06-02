/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Bass booster - sub-bass EQ with optional psychoacoustic harmonics
 * Fixed-point implementation for ARM targets (iPod Classic 6/7)
 *
 * Signal flow:
 *   Input -> LR4 crossover (2 cascaded LP biquads)
 *           -> Constant sub-bass boost (additive delta injection)
 *           -> Optional even-harmonic generator (MaxxBass-style)
 *           -> Output gain -> Soft clipper -> Output
 *
 * Dry mids/highs pass through; only the extracted sub band is boosted.
 * A master soft clipper limits peaks near full scale.
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

    int crossover_hz;        /* Hz, low-pass cutoff (default 100) */
    int sub_bass_gain;       /* 0-240, 0.1 dB sub boost (0 = off, default 120 = +12 dB) */
    int harmonics;           /* 0-100% harmonics mix (requires sub_bass_gain > 0) */
    int output_gain;         /* -120 to +120, 0.1 dB master trim (default 0) */
};

void dsp_set_bassboost(const struct bassboost_settings *settings);

#endif
