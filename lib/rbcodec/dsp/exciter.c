/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Air exciter - bandlimited even-harmonic generator for the treble
 * Fixed-point implementation for ARM targets (iPod Classic 6/7)
 *
 * Signal flow:
 *   Input -> LR4 high-pass (2 cascaded HP biquads)
 *           -> Even-harmonic generator (full-wave rectify + DC block)
 *           -> Scaled mix back into the dry signal -> Soft limiter
 *
 * Full-wave rectification of the isolated treble band creates sum and
 * difference products (the classic Aphex-style exciter spectrum); the
 * bandlimiting keeps those products in the treble so the result reads
 * as "air" rather than distortion.
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
#include <string.h>
#include "exciter.h"
#include "dsp_proc_entry.h"
#include "dsp_core.h"
#include "dsp_filter.h"
#include "dsp_sample_io.h"
#include "dsp_misc.h"
#include "platform.h"

#define UNITY              (1L << 24)
#define MAX_CH             2
#define CUTOFF_MIN_HZ      2000
#define CUTOFF_MAX_HZ      8000

static struct exciter_settings curr_set;
static struct dsp_filter hpf1, hpf2;

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
/*  Clamp cutoff to UI range and below Nyquist                        */
/* ------------------------------------------------------------------ */
static int clamp_cutoff_hz(int cutoff_hz, unsigned long fs)
{
    if (cutoff_hz < CUTOFF_MIN_HZ)
        cutoff_hz = CUTOFF_MIN_HZ;
    else if (cutoff_hz > CUTOFF_MAX_HZ)
        cutoff_hz = CUTOFF_MAX_HZ;

    if (fs >= 2)
    {
        unsigned long nyquist_max = fs / 2 - 1;
        if ((unsigned long)cutoff_hz > nyquist_max)
            cutoff_hz = (int)nyquist_max;
    }

    return cutoff_hz;
}

/* ------------------------------------------------------------------ */
/*  Filter setup: two cascaded Butterworth HP biquads (-24 dB/oct)    */
/* ------------------------------------------------------------------ */
static void setup_filter(int cutoff_hz, unsigned long fs)
{
    /* Same coefficient format as bassboost/crystalizer: cos/sin s0.31
     * -> s0.24, FRACMUL storage, shift=8. */
    unsigned long phase = fp_div(cutoff_hz, fs, 32);
    long cos_w0, sin_w0;
    sin_w0 = fp_sincos(phase, &cos_w0);

    int32_t alpha = (int32_t)(((int64_t)sin_w0 * (int64_t)0x5A82799ALL) >> 31);
    int32_t cos_w0_s24 = cos_w0 >> 7;
    int32_t alpha_s24   = alpha >> 7;

    int32_t hpc = (UNITY + cos_w0_s24) >> 1;
    int32_t b0 = hpc;
    int32_t b1 = -2 * hpc;
    int32_t b2 = hpc;
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

    hpf1.coefs[0] = coefs[0]; hpf2.coefs[0] = coefs[0];
    hpf1.coefs[1] = coefs[1]; hpf2.coefs[1] = coefs[1];
    hpf1.coefs[2] = coefs[2]; hpf2.coefs[2] = coefs[2];
    hpf1.coefs[3] = coefs[3]; hpf2.coefs[3] = coefs[3];
    hpf1.coefs[4] = coefs[4]; hpf2.coefs[4] = coefs[4];
    hpf1.shift = 8;           hpf2.shift = 8;

    /* DC blocker at 10 Hz for the harmonics generator */
    int32_t fc = 10;
    int32_t w = (fc * 2 * 31416) / 10000; /* 2 * pi * fc */
    dc_coeff = UNITY - (int32_t)(((int64_t)UNITY * w) / fs);
}

static void flush_filter(void)
{
    filter_flush(&hpf1);
    filter_flush(&hpf2);
    memset(dc_state, 0, sizeof(dc_state));
    memset(last_even_harm, 0, sizeof(last_even_harm));
}

/* ------------------------------------------------------------------ */
/*  Main processing callback                                          */
/* ------------------------------------------------------------------ */
static void exciter_process(struct dsp_proc_entry *this,
                            struct dsp_buffer **buf_p)
{
    (void)this;
    struct dsp_buffer *buf = *buf_p;
    int count      = buf->remcount;
    int32_t *out0  = buf->p32[0];
    int32_t *out1  = buf->p32[1];
    const int num_chan = MIN(buf->format.num_channels, MAX_CH);

    /* Full scale is 2^frac_bits; the soft limiter must track it (see
     * bassboost.c for the full rationale). */
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

            /* Isolate the treble band: two cascaded HP biquads */
            int32_t hf = biquad_step(&hpf1, ch, x);
            hf = biquad_step(&hpf2, ch, hf);

            /* Even-harmonic generation (MaxxBass rectifier, inverted):
             * full-wave rectify the band, then remove the DC offset */
            int64_t even_gen = (hf < 0) ? -(int64_t)hf : (int64_t)hf;

            int64_t hp_out = even_gen - last_even_harm[ch] +
                             ((dc_state[ch] * dc_coeff) >> 24);

            int64_t max_state = max_val << 4;
            if (hp_out > max_state) hp_out = max_state;
            else if (hp_out < -max_state) hp_out = -max_state;

            last_even_harm[ch] = even_gen;
            dc_state[ch] = hp_out;

            int64_t harm = (hp_out * harmonics_gain) >> 24;
            int64_t result64 = (int64_t)x + harm;

            /* Soft limiter: linear up to 7/8 FS, asymptotic knee above */
            int64_t abs_g = (result64 < 0) ? -result64 : result64;
            int32_t result;

            if (abs_g <= thresh)
            {
                result = (int32_t)result64;
            }
            else
            {
                int64_t over = abs_g - thresh;
                int64_t soft_over = headroom - (headroom * headroom) / (headroom + over);
                int64_t y = thresh + soft_over;
                if (y > max_val) y = max_val;
                result = (int32_t)((result64 < 0) ? -y : y);
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
static bool exciter_update(struct dsp_config *dsp,
                           const struct exciter_settings *settings)
{
    if (!settings->enabled)
        return false;

    unsigned long fs = dsp_get_output_frequency(dsp);
    if (fs <= 0)
        return false;

    curr_set = *settings;
    curr_set.cutoff_hz = clamp_cutoff_hz(curr_set.cutoff_hz, fs);

    setup_filter(curr_set.cutoff_hz, fs);

    harmonics_gain = ((int64_t)settings->intensity * UNITY) / 100;

    return true;
}

/* ------------------------------------------------------------------ */
/*  DSP configuration hook                                            */
/* ------------------------------------------------------------------ */
static intptr_t exciter_configure(struct dsp_proc_entry *this,
                                  struct dsp_config *dsp,
                                  unsigned int setting,
                                  intptr_t value)
{
    switch (setting)
    {
    case DSP_PROC_INIT:
        if (value != 0)
            break;
        this->process = exciter_process;
        exciter_update(dsp, &curr_set);
        break;

    case DSP_RESET:
    case DSP_FLUSH:
        flush_filter();
        break;

    case DSP_SET_OUT_FREQUENCY:
    case DSP_SET_FREQUENCY:
        exciter_update(dsp, &curr_set);
        break;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
void dsp_set_exciter(const struct exciter_settings *settings)
{
    struct dsp_config *dsp = dsp_get_config(CODEC_IDX_AUDIO);
    bool enable = exciter_update(dsp, settings);
    dsp_proc_enable(dsp, DSP_PROC_EXCITER, enable);
    dsp_proc_activate(dsp, DSP_PROC_EXCITER, true);
}

DSP_PROC_DB_ENTRY(EXCITER, exciter_configure);
