/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Air exciter - bandlimited even-harmonic generator for the treble
 *
 * Signal flow:
 *   Input -> LR4 high-pass (2 cascaded HP biquads)
 *           -> Even-harmonic generator (full-wave rectify + DC block)
 *           -> Scaled mix back into the dry signal -> Soft limiter
 *
 * Unlike a full-band soft-clip "exciter", only the isolated treble band
 * is driven non-linear, so the generated harmonics add air and sparkle
 * without intermodulating the mids and lows.
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
#ifndef EXCITER_H
#define EXCITER_H

#include <stdbool.h>

struct exciter_settings
{
    bool enabled;

    int cutoff_hz;           /* Hz, high-pass cutoff (default 3500) */
    int intensity;           /* 0-100% harmonics mix (default 30) */
};

void dsp_set_exciter(const struct exciter_settings *settings);

#endif
