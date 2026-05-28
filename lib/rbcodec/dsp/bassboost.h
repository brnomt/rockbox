/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Bass booster - upward compressor on sub-bass
 * Fixed-point implementation for ARM targets (iPod Classic 6/7)
 *
 * Signal flow:
 *   Input -> Sub-bass extraction (LPF) -> Envelope follower
 *           -> Upward compressor -> Mix with dry (volume-matched)
 *           -> Soft limit -> Output
 *
 * Low-pass filter isolates sub-bass. Upward compressor boosts bass
 * only when it falls below a threshold (-12 dB), making quiet bass
 * louder without increasing peaks. Volume matching keeps overall
 * level constant. No hard-clipping, no saturation, no distortion.
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
    bool ott_mode;           /* OTT-style: upward + downward compression toward target */
    int crossover_hz;        /* Hz, low-pass cutoff (default 100) */
    int sub_bass_gain;       /* 0-240, 0.1 dB max boost for quiet bass (default 120 = +12 dB) */
    int output_gain;         /* -120 to +120, 0.1 dB (default 0) */
};

void dsp_set_bassboost(const struct bassboost_settings *settings);

#endif
