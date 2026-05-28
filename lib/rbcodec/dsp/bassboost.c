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
 *   Input -> LR4 crossover (2 cascaded LP biquads) -> Envelope follower
 *           -> OTT-style upward compressor (ratio^4 curve)
 *           -> Additive mix (delta injection, dry path untouched)
 *           -> Output
 *
 * LR4 (-24 dB/octave) cleanly isolates sub-bass from mids/highs.
 * The upward compressor uses a convex ratio^4 transfer curve:
 * gain drops only near threshold, keeping bass dense and present
 * (OTT-style). Dry signal passes through unmodified — only the
 * processed delta is added back. Saturating addition prevents
 * int32 overflow on peaks. No hard-clipping, no distortion.
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

#define UNITY       (1L << 24)
#define MAX_CH      2

/* Upward compression threshold (fixed at -12 dB = 0.25 in Q24).
 * Bass below this level gets boosted; bass above passes through. */
#define COMP_THRESH ((int32_t)(UNITY / 4))

/* Envelope follower constants (Q24 fixed-point):
 * attack_coeff  = e^(-1 / (0.005 * fs))   ~ 5 ms
 * release_coeff = e^(-1 / (0.150 * fs))   ~ 150 ms */
#define ENV_ATTACK_MS   5
#define ENV_RELEASE_MS  150

static struct bassboost_settings curr_set;
static struct dsp_filter lpf1, lpf2;

static int32_t boost_gain     = UNITY;
static int32_t output_gain    = UNITY;

/* Envelope follower state (per channel) */
static int32_t env_state[MAX_CH];

/* Precomputed envelope coefficients (recalculated on sample rate change) */
static int32_t attack_coeff;
static int32_t release_coeff;

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
/*  Saturating addition: prevent int32 overflow from compressor gain    */
/* ------------------------------------------------------------------ */
static FORCE_INLINE int32_t sat_add(int32_t a, int32_t b)
{
    if (b > 0 && a > INT32_MAX - b)
        return INT32_MAX;
    if (b < 0 && a < INT32_MIN - b)
        return INT32_MIN;
    return a + b;
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
/*  Compute envelope follower coefficients from sample rate            */
/* ------------------------------------------------------------------ */
static void setup_envelope(unsigned long fs)
{
    /* coeff = e^(-1 / (time_s * fs))
     * Convert ms to 16.16 seconds, then use fp_factor */
    int32_t attack_t16 = (ENV_ATTACK_MS * 65536) / 1000;
    attack_coeff = fp_factor(-fp_div(65536, (long)attack_t16 * (long)fs, 16), 16) << 8;

    int32_t release_t16 = (ENV_RELEASE_MS * 65536) / 1000;
    release_coeff = fp_factor(-fp_div(65536, (long)release_t16 * (long)fs, 16), 16) << 8;
}

/* ------------------------------------------------------------------ */
/*  Filter setup: low-pass to isolate sub-bass content                 */
/* ------------------------------------------------------------------ */
static void setup_filter(int crossover_hz, unsigned long fs)
{
    /* LR4 crossover: two cascaded Butterworth LP biquads (-24 dB/oct).
     * Same format as crystalizer: cos/sin s0.31 -> s0.24,
     * FRACMUL storage, shift=6. */
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
    lpf1.shift = 6;           lpf2.shift = 6;
}

static void flush_filter(void)
{
    filter_flush(&lpf1);
    filter_flush(&lpf2);
    memset(env_state, 0, sizeof(env_state));
}

/* ------------------------------------------------------------------ */
/*  Main processing callback                                          */
/* ------------------------------------------------------------------ */
static void bassboost_process(struct dsp_proc_entry *this,
                               struct dsp_buffer **buf_p)
{
    (void)this;
    struct dsp_buffer *buf = *buf_p;
    int count     = buf->remcount;
    int32_t *out0 = buf->p32[0];
    int32_t *out1 = buf->p32[1];
    int num_ch    = buf->format.num_channels;

    for (int n = 0; n < count; n++)
    {
        int32_t L = out0[n];
        int32_t R = (num_ch > 1) ? out1[n] : L;
        int32_t outL, outR;

        for (int ch = 0; ch < num_ch; ch++)
        {
            int32_t x = (ch == 0) ? L : R;

            /* LR4 crossover: cascade two identical LP biquads
             * for -24 dB/octave sub-bass isolation */
            int32_t sub = biquad_step(&lpf1, ch, x);
            sub = biquad_step(&lpf2, ch, sub);

            /* Envelope follower (peak detection with attack/release) */
            int32_t level = (sub < 0) ? -sub : sub;
            int32_t coeff = (level > env_state[ch])
                          ? attack_coeff
                          : release_coeff;
            int32_t coeff_inv = UNITY - coeff;
            env_state[ch] = (int32_t)(
                ((int64_t)env_state[ch] * coeff) >> 24) +
                (int32_t)(((int64_t)level * coeff_inv) >> 24);

            /* OTT-style upward compressor: convex ratio^4 transfer curve.
             * Gain stays near max until close to threshold, then drops
             * sharply — creating dense, always-present sub-bass.
             *   gain = boost_gain - (boost_gain-1) * (env/thresh)^4    */
            int32_t wet = sub;
            if (boost_gain > UNITY && env_state[ch] < COMP_THRESH
                && env_state[ch] > 0)
            {
                int32_t ratio = (int32_t)(
                    ((int64_t)env_state[ch] * UNITY) / COMP_THRESH);
                int32_t ratio_sq = (int32_t)(
                    ((int64_t)ratio * ratio) >> 24);
                int32_t ratio_q = (int32_t)(
                    ((int64_t)ratio_sq * ratio_sq) >> 24);
                int32_t atten = (int32_t)(
                    ((int64_t)(boost_gain - UNITY) * ratio_q) >> 24);
                int32_t dyn_gain = boost_gain - atten;

                if (dyn_gain > (UNITY * 4))
                    dyn_gain = UNITY * 4;

                wet = (int32_t)(((int64_t)sub * dyn_gain) >> 24);
            }

            /* Additive mixing: inject only the extra bass gain into the
             * original signal. When wet = sub (no boost), output = x.
             * Saturating addition prevents int32 overflow on peaks. */
            int32_t delta_bass = wet - sub;
            int32_t result = sat_add(x, delta_bass);

            /* Output gain */
            if (output_gain != UNITY)
                result = (int32_t)(((int64_t)result * output_gain) >> 24);

            if (ch == 0) outL = result;
            else         outR = result;
        }

        if (num_ch == 1)
            outR = outL;

        out0[n] = outL;
        if (num_ch > 1)
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

    setup_filter(settings->crossover_hz, fs);
    setup_envelope(fs);

    /* Boost gain: 0-240 maps to 0-24 dB (default 120 = +12 dB) */
    boost_gain = db_tenths_to_gain(settings->sub_bass_gain);

    /* Output gain */
    output_gain = db_tenths_to_gain(settings->output_gain);

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
