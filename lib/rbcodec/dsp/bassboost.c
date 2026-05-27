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

#include "rbcodecconfig.h"
#include "fixedpoint.h"
#include "fracmul.h"
#include <string.h>
#include "bassboost.h"
#include "dsp_proc_entry.h"
#include "dsp_core.h"
#include "dsp_filter.h"
#include "dsp_sample_io.h"
#include "dsp_misc.h"
#include "platform.h"

#define UNITY (1L << 24)
#define MAX_CH 2

static struct bassboost_settings curr_set;
static int32_t output_gain = UNITY;
static int32_t wet_mix, dry_mix = UNITY;
static int32_t drive_gain = UNITY;                  /* Pre-gain into soft clipper (drive control) */
static struct dsp_filter lshelf;                    /* Low-shelf filter */

static FORCE_INLINE int32_t biquad_step(struct dsp_filter *f, int ch, int32_t x)
{
    int64_t acc = (int64_t)x * f->coefs[0];
    acc += (int64_t)f->history[ch][0] * f->coefs[1];
    acc += (int64_t)f->history[ch][1] * f->coefs[2];
    acc += (int64_t)f->history[ch][2] * f->coefs[3];
    acc += (int64_t)f->history[ch][3] * f->coefs[4];

    f->history[ch][1] = f->history[ch][0];
    f->history[ch][0] = x;
    f->history[ch][3] = f->history[ch][2];
    int32_t y = (int32_t)((acc << f->shift) >> 32);
    f->history[ch][2] = y;
    return y;
}

static void setup_lshelf(int cutoff_hz, int gain_db, unsigned long fs)
{
    unsigned long phase = fp_div(cutoff_hz, fs, 32);
    filter_ls_coefs(phase, 1, gain_db, &lshelf);
}

static void flush_filters(void)
{
    filter_flush(&lshelf);
}

static FORCE_INLINE int32_t soft_clip(int32_t x)
{
    if (x > UNITY)
        return UNITY;
    if (x < -UNITY)
        return -UNITY;

    int32_t x2 = FRACMUL_SHL(x, x, 7);
    int32_t coeff = (3 * UNITY / 2) - (x2 >> 1);
    return FRACMUL_SHL(x, coeff, 7);
}

static void bassboost_process(struct dsp_proc_entry *this,
                               struct dsp_buffer **buf_p)
{
    (void)this;
    struct dsp_buffer *buf = *buf_p;
    int count = buf->remcount;
    int32_t *out_l = buf->p32[0];
    int32_t *out_r = buf->p32[1];
    const int num_ch = buf->format.num_channels;

    for (int s = 0; s < count; s++)
    {
        int32_t L = out_l[s];
        int32_t R = (num_ch > 1) ? out_r[s] : L;

        int32_t outL = 0, outR = 0;

        for (int ch = 0; ch < num_ch; ch++)
        {
            int32_t x = (ch == 0) ? L : R;

            int32_t shelf = biquad_step(&lshelf, ch, x);

            int32_t driven = FRACMUL_SHL(shelf, drive_gain, 7);

            int32_t clipped = soft_clip(driven);

            int32_t mixed = FRACMUL_SHL(x, dry_mix, 7)
                          + FRACMUL_SHL(clipped, wet_mix, 7);

            if (output_gain != UNITY)
                mixed = FRACMUL_SHL(mixed, output_gain, 7);

            if (ch == 0) outL = mixed;
            else         outR = mixed;
        }

        if (num_ch == 1) outR = outL;

        out_l[s] = outL;
        if (num_ch > 1)
            out_r[s] = outR;
    }
}

static bool bassboost_update(struct dsp_config *dsp,
                              const struct bassboost_settings *settings)
{
    if (!settings->enabled)
        return false;

    int32_t fs = dsp_get_output_frequency(dsp);
    if (fs <= 0)
        return false;

    curr_set = *settings;

    setup_lshelf(settings->crossover_hz, settings->sub_bass_gain / 10, fs);

    int drv = settings->drive;
    if (drv > 0)
    {
        drive_gain = UNITY + FRACMUL_SHL(drv * 3, UNITY / 100, 7);
    }
    else
    {
        drive_gain = UNITY;
    }

    int mix = settings->mix;
    if (mix < 0) mix = 0;
    if (mix > 100) mix = 100;
    wet_mix = ((int64_t)mix * UNITY) / 100;
    dry_mix = UNITY - wet_mix;

    if (settings->output_gain != 0)
    {
        int32_t db_int = settings->output_gain / 10;
        int32_t db_frac = settings->output_gain % 10;
        int32_t db_s16 = (db_int << 16) + (db_frac * 6554);
        output_gain = fp_factor(db_s16, 16) << 8;
    }
    else
    {
        output_gain = UNITY;
    }

    return true;
}

static intptr_t bassboost_configure(struct dsp_proc_entry *this,
                                     struct dsp_config *dsp,
                                     unsigned int setting,
                                     intptr_t value)
{
    switch (setting)
    {
    case DSP_PROC_INIT:
        if (value != 0)
            break;
        this->process = bassboost_process;
        bassboost_update(dsp, &curr_set);

    case DSP_RESET:
    case DSP_FLUSH:
        flush_filters();
        break;

    case DSP_SET_OUT_FREQUENCY:
    case DSP_SET_FREQUENCY:
        bassboost_update(dsp, &curr_set);
        break;
    }

    return 0;
}

void dsp_set_bassboost(const struct bassboost_settings *settings)
{
    struct dsp_config *dsp = dsp_get_config(CODEC_IDX_AUDIO);
    bool enable = bassboost_update(dsp, settings);
    dsp_proc_enable(dsp, DSP_PROC_BASSBOOST, enable);
    dsp_proc_activate(dsp, DSP_PROC_BASSBOOST, true);
}

DSP_PROC_DB_ENTRY(BASSBOOST, bassboost_configure);
