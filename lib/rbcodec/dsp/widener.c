/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Stereo widener - mid/side width control with tamed low end
 * Fixed-point implementation for ARM targets (iPod Classic 6/7)
 *
 * Signal flow:
 *   L,R -> M/S decode -> Butterworth LP on the side signal
 *         -> side below crossover scaled by min(width, 100%)
 *         -> side above crossover scaled by width
 *         -> M/S encode -> Soft limiter
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

#include "rbcodecconfig.h"
#include "fixedpoint.h"
#include "fracmul.h"
#include "widener.h"
#include "dsp_proc_entry.h"
#include "dsp_core.h"
#include "dsp_filter.h"
#include "dsp_sample_io.h"
#include "dsp_misc.h"
#include "platform.h"

#define UNITY              (1L << 24)
#define CROSSOVER_MIN_HZ   50
#define CROSSOVER_MAX_HZ   500

static struct widener_settings curr_set;
static struct dsp_filter side_lpf;

static int32_t width_gain     = UNITY;
static int32_t width_low_gain = UNITY;

/* ------------------------------------------------------------------ */
/*  Per-sample biquad step (direct form 1, channel 0 only)            */
/* ------------------------------------------------------------------ */
static FORCE_INLINE int32_t biquad_step(struct dsp_filter *f, int32_t x)
{
    int64_t acc  = (int64_t)x * f->coefs[0];
    acc += (int64_t)f->history[0][0] * f->coefs[1];
    acc += (int64_t)f->history[0][1] * f->coefs[2];
    acc += (int64_t)f->history[0][2] * f->coefs[3];
    acc += (int64_t)f->history[0][3] * f->coefs[4];

    f->history[0][1] = f->history[0][0];
    f->history[0][0] = x;
    f->history[0][3] = f->history[0][2];

    int32_t y = (int32_t)((acc << f->shift) >> 32);
    f->history[0][2] = y;
    return y;
}

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

/* Butterworth low-pass coefficients for the side signal split; same
 * format as bassboost/crystalizer (s0.24, FRACMUL storage, shift=8). */
static void setup_filter(int crossover_hz, unsigned long fs)
{
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

    side_lpf.coefs[0] = FRACMUL(b0, rcp_a0);
    side_lpf.coefs[1] = FRACMUL(b1, rcp_a0);
    side_lpf.coefs[2] = FRACMUL(b2, rcp_a0);
    side_lpf.coefs[3] = FRACMUL(-a1, rcp_a0);
    side_lpf.coefs[4] = FRACMUL(-a2, rcp_a0);
    side_lpf.shift = 8;
}

static void flush_filter(void)
{
    filter_flush(&side_lpf);
}

/* ------------------------------------------------------------------ */
/*  Main processing callback                                          */
/* ------------------------------------------------------------------ */
static void widener_process(struct dsp_proc_entry *this,
                            struct dsp_buffer **buf_p)
{
    (void)this;
    struct dsp_buffer *buf = *buf_p;
    int count      = buf->remcount;
    int32_t *out0  = buf->p32[0];
    int32_t *out1  = buf->p32[1];

    /* Widening needs a stereo image; mono passes through untouched. */
    if (buf->format.num_channels < 2)
        return;

    /* Full scale is 2^frac_bits (27 for 16-bit sources). */
    const int64_t max_val = ((int64_t)1 << buf->format.frac_bits) - 1;

    for (int n = 0; n < count; n++)
    {
        int32_t L = out0[n];
        int32_t R = out1[n];

        /* Mid/side decode. Work in int64 shifted down one bit so the
         * sum and difference of two near-full-scale samples cannot
         * overflow; the level is restored at the encode step. */
        int64_t mid  = ((int64_t)L + R) >> 1;
        int64_t side = ((int64_t)L - R) >> 1;

        /* Split the side signal: low-passed part keeps at most unity
         * width (bass stays mono-compatible), the rest follows width. */
        int32_t side_in = (int32_t)side;
        int32_t side_low = biquad_step(&side_lpf, side_in);
        int64_t side_high = side - side_low;

        int64_t side_out = ((int64_t)side_low * width_low_gain >> 24)
                         + (side_high * width_gain >> 24);

        int64_t newL = mid + side_out;
        int64_t newR = mid - side_out;

        /* Clamp in 64-bit before narrowing (casting first would wrap).
         * Like upstream stages, peak control is left to the compressor
         * that follows in the chain; a per-sample soft knee here cost a
         * 64-bit divide and coloured every loud master above -1 dBFS. */
        if (newL > max_val)       newL = max_val;
        else if (newL < -max_val) newL = -max_val;
        if (newR > max_val)       newR = max_val;
        else if (newR < -max_val) newR = -max_val;

        out0[n] = (int32_t)newL;
        out1[n] = (int32_t)newR;
    }
}

/* ------------------------------------------------------------------ */
/*  Update from settings                                              */
/* ------------------------------------------------------------------ */
static bool widener_update(struct dsp_config *dsp,
                           const struct widener_settings *settings)
{
    /* 100% width is an exact pass-through: don't run the stage at all. */
    if (!settings->enabled || settings->width == 100)
        return false;

    unsigned long fs = dsp_get_output_frequency(dsp);
    if (fs <= 0)
        return false;

    curr_set = *settings;
    curr_set.crossover_hz = clamp_crossover_hz(curr_set.crossover_hz, fs);

    setup_filter(curr_set.crossover_hz, fs);

    int width = settings->width;
    if (width < 0) width = 0;
    if (width > 200) width = 200;
    width_gain = ((int64_t)width * UNITY) / 100;
    /* Bass never widens beyond its original width */
    width_low_gain = (width_gain < UNITY) ? width_gain : UNITY;

    return true;
}

/* ------------------------------------------------------------------ */
/*  DSP configuration hook                                            */
/* ------------------------------------------------------------------ */
static intptr_t widener_configure(struct dsp_proc_entry *this,
                                  struct dsp_config *dsp,
                                  unsigned int setting,
                                  intptr_t value)
{
    switch (setting)
    {
    case DSP_PROC_INIT:
        if (value != 0)
            break; /* Already enabled */
        /* Settings were just computed by dsp_set_widener(). */
        this->process = widener_process;
        /* Fall-through */
    case DSP_RESET:
    case DSP_FLUSH:
        flush_filter();
        break;

    /* Only the output rate matters (resampler runs before this stage). */
    case DSP_SET_OUT_FREQUENCY:
        widener_update(dsp, &curr_set);
        break;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
void dsp_set_widener(const struct widener_settings *settings)
{
    struct dsp_config *dsp = dsp_get_config(CODEC_IDX_AUDIO);
    bool enable = widener_update(dsp, settings);
    dsp_proc_enable(dsp, DSP_PROC_WIDENER, enable);
    dsp_proc_activate(dsp, DSP_PROC_WIDENER, true);
}

DSP_PROC_DB_ENTRY(WIDENER, widener_configure);
