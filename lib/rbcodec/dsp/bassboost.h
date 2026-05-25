/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Bass booster - single-band compressor for low frequencies
 * Fixed-point implementation for ARM targets (iPod Classic 6/7)
 *
 * Signal flow:
 *   Input → LR2 LPF (crossover) → Bass band → Drive/Saturation
 *       → Pre-gain (detector) → Compress (up + down) → Mix → Output
 *
 * Upward expansion: boosts quiet sub-bass content toward threshold
 * Downward compression: tames peaks above threshold
 * Dual envelope follower: separate attack/release for each direction
 * Drive: cubic soft-clip for audible harmonic saturation
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
    int crossover_hz;       /* Hz, low-pass cutoff (default 120) */
    int pre_gain;           /* 0-240, 0.1 dB boost before envelope detection (default 0) */
    int threshold;          /* dB value, 0 = off, -60 to 0 (default -18) */
    int ratio_down;         /* 0=off,1=2:1,2=4:1,3=6:1,4=10:1,5=limit (default 2=4:1) */
    int ratio_up;           /* 0=off,1=0.25:1,2=0.5:1,3=0.75:1 (default 2=0.5:1) */
    int attack_ms;          /* 1-500 (default 5) */
    int release_ms;         /* 10-2000 (default 150) */
    int knee_db;            /* 0-12 (default 6) */
    int drive;              /* 0-100 saturation amount (default 0) */
    int makeup_gain_db;     /* 0-240, 0.1 dB (default 60 = +6dB) */
    int mix;                /* 0-100% (default 100) */
    int spread;              /* 0-100% harmonic spread to full-band (default 0) */
    int output_gain;         /* -120 to +120, 0.1 dB (default 0) */
};

void dsp_set_bassboost(const struct bassboost_settings *settings);

#endif
