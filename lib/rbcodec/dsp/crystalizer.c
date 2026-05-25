/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Crystalizer - fixed-point multiband transient enhancer
 *
 * Signal flow:
 *   Input → LR2 crossover network (3 bands) → 2nd-derivative enhancement
 *       → sum bands → output gain
 *
 * Bands:
 *   Low:  0 - crossover_low    Hz
 *   Mid:  crossover_low - crossover_mid Hz
 *   High: crossover_mid+       Hz
 *
 * For each band, the backward-difference second derivative is computed
 * sample-by-sample: d2[n] = x[n] - 2*x[n-1] + x[n-2].
 * Enhanced output = x[n] + intensity * d2[n].
 *
 * This sharpens transients (peaks and onsets) by boosting the curvature
 * of the waveform, following the EasyEffects Crystalizer approach.
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
#include "crystalizer.h"
#include "dsp_proc_entry.h"
#include "dsp_core.h"
#include "dsp_filter.h"
#include "dsp_sample_io.h"
#include "dsp_misc.h"
#include "platform.h"

#define UNITY (1L << 24)
#define MAX_CH 2
#define NUM_BANDS 3

static struct crystalizer_settings curr_set;
static int32_t output_gain = UNITY;
static int32_t wet_mix, dry_mix = UNITY;

static int32_t intensity_linear[NUM_BANDS];

/* Crossover filters: LR2 at low_cut and mid_cut */
static struct dsp_filter lpf_low[2];
static struct dsp_filter lpf_mid[2];

/* Per-band per-channel history for backward second derivative */
static int32_t band_x1[NUM_BANDS][MAX_CH];
static int32_t band_x2[NUM_BANDS][MAX_CH];

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

static void butterworth_coefs(unsigned long cutoff_phase, bool highpass,
                               struct dsp_filter *f)
{
    long cos_w0, sin_w0;
    sin_w0 = fp_sincos(cutoff_phase, &cos_w0);

    int32_t alpha = (int32_t)(((int64_t)sin_w0 * (int64_t)0x5A82799ALL) >> 31);
    int32_t cos_w0_s24 = cos_w0 >> 7;
    int32_t alpha_s24   = alpha >> 7;

    int32_t b0, b1, b2, a0, a1, a2;

    if (highpass)
    {
        int32_t hpc = (UNITY + cos_w0_s24) >> 1;
        b0 = hpc;
        b1 = -2 * hpc;
        b2 = hpc;
    }
    else
    {
        int32_t lpc = (UNITY - cos_w0_s24) >> 1;
        b0 = lpc;
        b1 = 2 * lpc;
        b2 = lpc;
    }

    a0 = UNITY + alpha_s24;
    a1 = -2 * cos_w0_s24;
    a2 = UNITY - alpha_s24;

    int32_t rcp_a0 = (int32_t)(((int64_t)1 << 55) / (int64_t)a0);

    f->coefs[0] = FRACMUL(b0, rcp_a0);
    f->coefs[1] = FRACMUL(b1, rcp_a0);
    f->coefs[2] = FRACMUL(b2, rcp_a0);
    f->coefs[3] = FRACMUL(-a1, rcp_a0);
    f->coefs[4] = FRACMUL(-a2, rcp_a0);
    f->shift = 6;
}

static void setup_filters(int low_hz, int mid_hz, unsigned long fs)
{
    unsigned long phase_low = fp_div(low_hz, fs, 32);
    butterworth_coefs(phase_low, false, &lpf_low[0]);
    butterworth_coefs(phase_low, false, &lpf_low[1]);

    unsigned long phase_mid = fp_div(mid_hz, fs, 32);
    butterworth_coefs(phase_mid, false, &lpf_mid[0]);
    butterworth_coefs(phase_mid, false, &lpf_mid[1]);
}

static void flush_filters(void)
{
    filter_flush(&lpf_low[0]);
    filter_flush(&lpf_low[1]);
    filter_flush(&lpf_mid[0]);
    filter_flush(&lpf_mid[1]);
}

static void crystalizer_process(struct dsp_proc_entry *this,
                                 struct dsp_buffer **buf_p)
{
    (void)this;
    struct dsp_buffer *buf = *buf_p;
    int count = buf->remcount;
    int32_t *out_l = buf->p32[0];
    int32_t *out_r = buf->p32[1];
    const int num_ch = buf->format.num_channels;
    const int frac_bits = buf->format.frac_bits;
    (void)frac_bits;

    for (int s = 0; s < count; s++)
    {
        int32_t L = out_l[s];
        int32_t R = (num_ch > 1) ? out_r[s] : L;

        int32_t outL = 0, outR = 0;

        for (int ch = 0; ch < num_ch; ch++)
        {
            int32_t x = (ch == 0) ? L : R;

            int32_t lp_low = biquad_step(&lpf_low[0], ch, x);
            lp_low = biquad_step(&lpf_low[1], ch, lp_low);

            int32_t hp_low = x - lp_low;

            int32_t lp_mid = biquad_step(&lpf_mid[0], ch, hp_low);
            lp_mid = biquad_step(&lpf_mid[1], ch, lp_mid);

            int32_t hp_mid = hp_low - lp_mid;

            int32_t bands[NUM_BANDS];
            bands[0] = lp_low;
            bands[1] = lp_mid;
            bands[2] = hp_mid;

            int32_t sum = 0;

            for (int b = 0; b < NUM_BANDS; b++)
            {
                int32_t d2 = bands[b] - 2 * band_x1[b][ch] + band_x2[b][ch];

                band_x2[b][ch] = band_x1[b][ch];
                band_x1[b][ch] = bands[b];

                if (intensity_linear[b] != 0 && d2 != 0)
                {
                    int32_t enh = FRACMUL_SHL(intensity_linear[b], d2, 7);
                    sum += bands[b] + enh;
                }
                else
                {
                    sum += bands[b];
                }
            }

            int32_t merged = FRACMUL_SHL(x, dry_mix, 7)
                           + FRACMUL_SHL(sum, wet_mix, 7);

            if (ch == 0) outL = merged;
            else         outR = merged;
        }

        if (num_ch == 1) outR = outL;

        if (output_gain != UNITY)
        {
            outL = FRACMUL_SHL(outL, output_gain, 7);
            outR = FRACMUL_SHL(outR, output_gain, 7);
        }

        out_l[s] = outL;
        if (num_ch > 1)
            out_r[s] = outR;
    }
}

static bool crystalizer_update(struct dsp_config *dsp,
                                const struct crystalizer_settings *settings)
{
    if (!settings->enabled)
        return false;

    int32_t fs = dsp_get_output_frequency(dsp);
    if (fs <= 0)
        return false;

    curr_set = *settings;

    setup_filters(300, 3000, fs);

    static const int band_indices[NUM_BANDS] = {0, 1, 2};
    (void)band_indices;

    for (int b = 0; b < NUM_BANDS; b++)
    {
        int val;
        switch (b)
        {
        case 0: val = settings->intensity_low;  break;
        case 1: val = settings->intensity_mid;  break;
        default: val = settings->intensity_high; break;
        }

        if (val != 0)
        {
            int32_t db_int  = val / 10;
            int32_t db_frac = val % 10;
            int32_t db_s16  = (db_int << 16) + (db_frac * 6554);
            intensity_linear[b] = fp_factor(db_s16, 16) << 8;
        }
        else
        {
            intensity_linear[b] = 0;
        }
    }

    if (settings->output_gain != 0)
    {
        int32_t db_int  = settings->output_gain / 10;
        int32_t db_frac = settings->output_gain % 10;
        int32_t db_s16  = (db_int << 16) + (db_frac * 6554);
        output_gain = fp_factor(db_s16, 16) << 8;
    }
    else
    {
        output_gain = UNITY;
    }

    int mix = settings->mix;
    if (mix < 0) mix = 0;
    if (mix > 100) mix = 100;
    wet_mix = ((int64_t)mix * UNITY) / 100;
    dry_mix = UNITY - wet_mix;

    return true;
}

static intptr_t crystalizer_configure(struct dsp_proc_entry *this,
                                       struct dsp_config *dsp,
                                       unsigned int setting,
                                       intptr_t value)
{
    switch (setting)
    {
    case DSP_PROC_INIT:
        if (value != 0)
            break;
        this->process = crystalizer_process;
        crystalizer_update(dsp, &curr_set);

    case DSP_RESET:
    case DSP_FLUSH:
        for (int b = 0; b < NUM_BANDS; b++)
        {
            for (int ch = 0; ch < MAX_CH; ch++)
            {
                band_x1[b][ch] = 0;
                band_x2[b][ch] = 0;
            }
        }
        flush_filters();
        break;

    case DSP_SET_OUT_FREQUENCY:
    case DSP_SET_FREQUENCY:
        crystalizer_update(dsp, &curr_set);
        break;
    }

    return 0;
}

void dsp_set_crystalizer(const struct crystalizer_settings *settings)
{
    struct dsp_config *dsp = dsp_get_config(CODEC_IDX_AUDIO);
    bool enable = crystalizer_update(dsp, settings);
    dsp_proc_enable(dsp, DSP_PROC_CRYSTALIZER, enable);
    dsp_proc_activate(dsp, DSP_PROC_CRYSTALIZER, true);
}

DSP_PROC_DB_ENTRY(CRYSTALIZER, crystalizer_configure);
