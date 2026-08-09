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
 * LR4 (-24 dB/octave) isolates sub-bass. Boost is applied to the full
 * sub band (not dynamics-dependent). Harmonics require sub_bass_gain > 0.
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

#define UNITY              (1L << 24)
#define MAX_CH             2
#define CROSSOVER_MIN_HZ   40
#define CROSSOVER_MAX_HZ   500

static struct bassboost_settings curr_set;
static struct dsp_filter lpf1, lpf2;

static int32_t boost_gain     = UNITY;
static int32_t output_gain    = UNITY;

/* Psychoacoustic Harmonics Generator */
static int32_t harmonics_gain = 0;
static int64_t dc_state[MAX_CH];
static int64_t last_even_harm[MAX_CH];
static int32_t dc_coeff;

/* ------------------------------------------------------------------ */
/*  Per-sample biquad step (direct form 1)                            */
/* ------------------------------------------------------------------ */
static FORCE_INLINE int32_t biquad_step(struct dsp_filter *f, int ch, int32_t x)
{
    int64_t acc  = (int64_t)x * f->coefs[0];
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

/* ------------------------------------------------------------------ */
/*  Convert decibels-tenths to Q24 gain factor                        */
/* ------------------------------------------------------------------ */
static int32_t db_tenths_to_gain(int db_tenths)
{
    if (db_tenths == 0)
        return UNITY;

    int sign     = (db_tenths > 0) ? 1 : -1;
    int abs_db   = db_tenths * sign;
    int db_int   = abs_db / 10;
    int db_frac  = abs_db % 10;

    int32_t db_s16 = sign * ((db_int << 16) + (db_frac * 6554));
    return fp_factor(db_s16, 16) << 8;
}

/* ------------------------------------------------------------------ */
/*  Clamp crossover to UI range and below Nyquist                     */
/* ------------------------------------------------------------------ */
static int clamp_crossover_hz(int crossover_hz, unsigned long fs)
{
    if (crossover_hz < CROSSOVER_MIN_HZ)
        crossover_hz = CROSSOVER_MIN_HZ;
    else if (crossover_hz > CROSSOVER_MAX_HZ)
        crossover_hz = CROSSOVER_MAX_HZ;

    if (fs >= 2)
    {
        unsigned long nyquist_max = fs / 2 - 1;
        if (nyquist_max < (unsigned long)CROSSOVER_MIN_HZ)
            nyquist_max = CROSSOVER_MIN_HZ;
        if ((unsigned long)crossover_hz > nyquist_max)
            crossover_hz = (int)nyquist_max;
    }

    return crossover_hz;
}

/* ------------------------------------------------------------------ */
/*  Filter setup: low-pass to isolate sub-bass content                 */
/* ------------------------------------------------------------------ */
static void setup_filter(int crossover_hz, unsigned long fs)
{
    /* LR4 crossover: two cascaded Butterworth LP biquads (-24 dB/oct).
     * Same format as crystalizer: cos/sin s0.31 -> s0.24,
     * FRACMUL storage, shift=8. */
    unsigned long phase = fp_div(crossover_hz, fs, 32);
    long cos_w0, sin_w0;
    sin_w0 = fp_sincos(phase, &cos_w0);

    int32_t alpha = (int32_t)(((int64_t)sin_w0 * (int64_t)0x5A82799ALL) >> 31);
    int32_t cos_w0_s24 = cos_w0 >> 7;
    int32_t alpha_s24   = alpha >> 7;

    int32_t lpc = (UNITY - cos_w0_s24) >> 1;
    int32_t b0 = lpc;
    int32_t b1 = 2 * lpc;
    int32_t b2 = lpc;
    int32_t a0 = UNITY + alpha_s24;
    int32_t a1 = -2 * cos_w0_s24;
    int32_t a2 = UNITY - alpha_s24;

    int32_t rcp_a0 = (int32_t)(((int64_t)1 << 55) / (int64_t)a0);

    int32_t coefs[5];
    coefs[0] = FRACMUL(b0, rcp_a0);
    coefs[1] = FRACMUL(b1, rcp_a0);
    coefs[2] = FRACMUL(b2, rcp_a0);
    coefs[3] = FRACMUL(-a1, rcp_a0);
    coefs[4] = FRACMUL(-a2, rcp_a0);

    lpf1.coefs[0] = coefs[0]; lpf2.coefs[0] = coefs[0];
    lpf1.coefs[1] = coefs[1]; lpf2.coefs[1] = coefs[1];
    lpf1.coefs[2] = coefs[2]; lpf2.coefs[2] = coefs[2];
    lpf1.coefs[3] = coefs[3]; lpf2.coefs[3] = coefs[3];
    lpf1.coefs[4] = coefs[4]; lpf2.coefs[4] = coefs[4];
    lpf1.shift = 8;           lpf2.shift = 8;

    /* DC blocker at 10 Hz for the harmonics generator */
    int32_t fc = 10;
    int32_t w = (fc * 2 * 31416) / 10000; /* 2 * pi * fc */
    dc_coeff = UNITY - (int32_t)(((int64_t)UNITY * w) / fs);
}

static void flush_filter(void)
{
    filter_flush(&lpf1);
    filter_flush(&lpf2);
    memset(dc_state, 0, sizeof(dc_state));
    memset(last_even_harm, 0, sizeof(last_even_harm));
}

/* ------------------------------------------------------------------ */
/*  Main processing callback                                          */
/* ------------------------------------------------------------------ */
static void bassboost_process(struct dsp_proc_entry *this,
                               struct dsp_buffer **buf_p)
{
    (void)this;
    struct dsp_buffer *buf = *buf_p;
    int count      = buf->remcount;
    int32_t *out0  = buf->p32[0];
    int32_t *out1  = buf->p32[1];
    const int num_chan = MIN(buf->format.num_channels, MAX_CH);

    /* Full scale is 2^frac_bits (2^27 for 16-bit sources, 2^31 for 32-bit).
     * The limiter must track it: a fixed 2^31 threshold sits far above the
     * real full scale of 16-bit audio and never engages, so boosted peaks
     * hard-clip at the output conversion instead of being soft-limited. */
    const int frac_bits = buf->format.frac_bits;
    const int64_t max_val  = ((int64_t)1 << frac_bits) - 1;
    const int64_t thresh   = (max_val * 7) >> 3;   /* 7/8 FS, ~ -1.9 dBFS */
    const int64_t headroom = max_val - thresh;

    for (int n = 0; n < count; n++)
    {
        int32_t L = out0[n];
        int32_t R = (num_chan > 1) ? out1[n] : L;
        int32_t outL = 0, outR = 0;

        for (int ch = 0; ch < num_chan; ch++)
        {
            int32_t x = (ch == 0) ? L : R;

            /* LR4 crossover: cascade two identical LP biquads
             * for -24 dB/octave sub-bass isolation */
            int32_t sub = biquad_step(&lpf1, ch, x);
            sub = biquad_step(&lpf2, ch, sub);

            /* Constant sub-bass boost (additive delta injection) */
            int64_t wet = ((int64_t)sub * boost_gain) >> 24;

            /* Psychoacoustic Harmonics (MaxxBass principle) */
            int64_t harm = 0;
            if (harmonics_gain > 0)
            {
                /* Generate even harmonics from original sub-bass (not amplified) */
                int64_t even_gen = (sub < 0) ? -(int64_t)sub : (int64_t)sub;

                /* 1-pole DC Blocker to remove the 0 Hz offset */
                int64_t hp_out = even_gen - last_even_harm[ch] +
                                 ((dc_state[ch] * dc_coeff) >> 24);

                /* Clamp DC state relative to full scale. Normal operation keeps
                 * hp_out near FS; this only guards against integrator windup,
                 * and a tighter bound keeps the later gain math overflow-safe. */
                int64_t max_state = max_val << 4;
                if (hp_out > max_state) hp_out = max_state;
                else if (hp_out < -max_state) hp_out = -max_state;

                last_even_harm[ch] = even_gen;
                dc_state[ch] = hp_out;

                harm = (hp_out * harmonics_gain) >> 24;
            }

            /* When wet = sub (no boost), output = x unless harmonics are on */
            int64_t delta_bass = wet - sub;
            int64_t raw_result = (int64_t)x + delta_bass + harm;

            /* Pre-shift raw_result to avoid 64-bit overflow with large output_gain */
            int64_t g_result = (raw_result >> 8) * output_gain;
            g_result >>= 16; /* 8 + 16 = 24 bits for Q24 */

            /* Master soft clipper: linear up to 7/8 FS, soft knee above. The
             * knee is asymptotic toward max_val, so output never hard-clips. */
            int64_t abs_g = (g_result < 0) ? -g_result : g_result;
            int32_t result;

            if (abs_g <= thresh)
            {
                result = (int32_t)g_result;
            }
            else
            {
                int64_t over = abs_g - thresh;
                int64_t soft_over = headroom - (headroom * headroom) / (headroom + over);
                int64_t y = thresh + soft_over;
                if (y > max_val) y = max_val;
                result = (int32_t)((g_result < 0) ? -y : y);
            }

            if (ch == 0) outL = result;
            else         outR = result;
        }

        if (num_chan == 1)
            outR = outL;

        out0[n] = outL;
        if (num_chan > 1)
            out1[n] = outR;
    }
}

/* ------------------------------------------------------------------ */
/*  Update from settings                                              */
/* ------------------------------------------------------------------ */
static bool bassboost_update(struct dsp_config *dsp,
                              const struct bassboost_settings *settings)
{
    if (!settings->enabled)
        return false;

    unsigned long fs = dsp_get_output_frequency(dsp);
    if (fs <= 0)
        return false;

    curr_set = *settings;
    curr_set.crossover_hz = clamp_crossover_hz(curr_set.crossover_hz, fs);

    setup_filter(curr_set.crossover_hz, fs);

    /* Boost gain: 0-240 maps to 0-24 dB (default 120 = +12 dB) */
    boost_gain = db_tenths_to_gain(settings->sub_bass_gain);
    if (boost_gain > (UNITY * 16))
        boost_gain = UNITY * 16;

    /* Harmonics only when sub boost is active */
    if (settings->sub_bass_gain > 0 && settings->harmonics > 0)
        harmonics_gain = ((int64_t)settings->harmonics * UNITY) / 100;
    else
        harmonics_gain = 0;

    /* Output gain */
    output_gain = db_tenths_to_gain(settings->output_gain);
    if (output_gain > (UNITY * 16))
        output_gain = UNITY * 16;

    return true;
}

/* ------------------------------------------------------------------ */
/*  DSP configuration hook                                            */
/* ------------------------------------------------------------------ */
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
        break;

    case DSP_RESET:
    case DSP_FLUSH:
        flush_filter();
        break;

    case DSP_SET_OUT_FREQUENCY:
    case DSP_SET_FREQUENCY:
        bassboost_update(dsp, &curr_set);
        break;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
void dsp_set_bassboost(const struct bassboost_settings *settings)
{
    struct dsp_config *dsp = dsp_get_config(CODEC_IDX_AUDIO);
    bool enable = bassboost_update(dsp, settings);
    dsp_proc_enable(dsp, DSP_PROC_BASSBOOST, enable);
    dsp_proc_activate(dsp, DSP_PROC_BASSBOOST, true);
}

DSP_PROC_DB_ENTRY(BASSBOOST, bassboost_configure);
