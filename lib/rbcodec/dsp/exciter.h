/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Harmonic exciter for high frequencies
 * Fixed-point implementation for ARM targets
 *
 * Signal flow:
 *   Input -> HPF (scope) -> Drive/Saturation (cubic soft-clip)
 *         -> LPF (ceil, optional) -> Mix wet/dry -> Output
 *
 * Based on Calf Exciter (tap_distortion + RBJ filters),
 * simplified to cubic soft-clip for fixed-point efficiency.
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
#ifndef EXCITER_H
#define EXCITER_H

#include <stdbool.h>

struct exciter_settings
{
    bool enabled;
    int scope_hz;        /* Hz, HPF cutoff (default 3000) */
    int drive;            /* 0-100 saturation amount (default 30) */
    int amount;           /* 0-120, 0.1 dB wet level (default 0) */
    int ceil_hz;          /* Hz LPF ceiling, 0=off (default 0) */
    int output_gain;      /* -120 to +120, 0.1 dB (default 0) */
};

void dsp_set_exciter(const struct exciter_settings *settings);

#endif /* EXCITER_H */
