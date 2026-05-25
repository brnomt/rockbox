/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Harmonic exciter - fixed-point implementation
 *   Input -> HPF (scope) -> Drive/Saturation (cubic soft-clip)
 *         -> LPF (ceil, optional) -> Mix wet/dry -> Output
 *
 * Based on Calf Exciter, simplified to cubic soft-clip.
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
#include "exciter.h"
#include "dsp_proc_entry.h"
#include "dsp_core.h"
#include "dsp_filter.h"
#include "dsp_sample_io.h"
#include "dsp_misc.h"
#include "platform.h"

#define UNITY (1L << 24)
#define MAX_CH 2

static struct exciter_settings curr_set;
static int32_t output_gain = UNITY;
static int32_t drive_scale = UNITY, drive_blend;
static int32_t wet_gain, dry_gain = UNITY;
static struct dsp_filter hpf[2];
static struct dsp_filter lpf[2];

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

static void setup_hpf(int cutoff_hz, unsigned long fs)
{
    if (cutoff_hz <= 0)
        cutoff_hz = 1;
    unsigned long phase = fp_div(cutoff_hz, fs, 32);
    butterworth_coefs(phase, true, &hpf[0]);
    butterworth_coefs(phase, true, &hpf[1]);
}

static void setup_lpf(int cutoff_hz, unsigned long fs)
{
    if (cutoff_hz <= 0)
        cutoff_hz = 1;
    unsigned long phase = fp_div(cutoff_hz, fs, 32);
    butterworth_coefs(phase, false, &lpf[0]);
    butterworth_coefs(phase, false, &lpf[1]);
}

static void flush_filters(void)
{
    filter_flush(&hpf[0]);
    filter_flush(&hpf[1]);
    filter_flush(&lpf[0]);
    filter_flush(&lpf[1]);
}

static FORCE_INLINE int32_t apply_drive(int32_t x, int32_t scale, int32_t blend)
{
    if (blend <= 0)
        return x;

    int32_t x_driven = FRACMUL_SHL(x, scale, 7);
    if (x_driven > UNITY)
        x_driven = UNITY;
    else if (x_driven < -UNITY)
        x_driven = -UNITY;

    int32_t x2 = FRACMUL_SHL(x_driven, x_driven, 7);
    int32_t x3 = FRACMUL_SHL(x2, x_driven, 7);
    int32_t sat = (3 * (int64_t)x_driven - x3) >> 1;

    int32_t dry_part = FRACMUL_SHL(x, UNITY - blend, 7);
    int32_t wet_part = FRACMUL_SHL(sat, blend, 7);
    return dry_part + wet_part;
}

static void exciter_process(struct dsp_proc_entry *this,
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

            int32_t hp = biquad_step(&hpf[0], ch, x);
            hp = biquad_step(&hpf[1], ch, hp);

            hp = apply_drive(hp, drive_scale, drive_blend);

            if (curr_set.ceil_hz > 0)
            {
                hp = biquad_step(&lpf[1], ch,
                      biquad_step(&lpf[0], ch, hp));
            }

            int32_t merged = FRACMUL_SHL(x, dry_gain, 7)
                           + FRACMUL_SHL(hp, wet_gain, 7);

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

static bool exciter_update(struct dsp_config *dsp,
                            const struct exciter_settings *settings)
{
    if (!settings->enabled)
        return false;

    int32_t fs = dsp_get_output_frequency(dsp);
    if (fs <= 0)
        return false;

    curr_set = *settings;

    setup_hpf(settings->scope_hz, fs);

    if (settings->ceil_hz > 0)
        setup_lpf(settings->ceil_hz, fs);

    int drv = settings->drive;
    if (drv > 0)
    {
        drive_scale = UNITY + FRACMUL_SHL(drv * 3, UNITY / 100, 7);
        drive_blend = FRACMUL_SHL(drv, UNITY / 100, 7);
    }
    else
    {
        drive_scale = UNITY;
        drive_blend = 0;
    }

    if (settings->amount > 0)
    {
        int32_t db_d10 = settings->amount;
        int32_t db_int  = db_d10 / 10;
        int32_t db_frac = db_d10 % 10;
        int32_t db_s16 = (db_int << 16) + (db_frac * 6554);
        wet_gain = fp_factor(db_s16, 16) << 8;
    }
    else
    {
        wet_gain = 0;
    }
    dry_gain = UNITY;

    if (settings->output_gain != 0)
    {
        int32_t db_d10 = settings->output_gain;
        int32_t db_int  = db_d10 / 10;
        int32_t db_frac = db_d10 % 10;
        int32_t db_s16 = (db_int << 16) + (db_frac * 6554);
        output_gain = fp_factor(db_s16, 16) << 8;
    }
    else
    {
        output_gain = UNITY;
    }

    return true;
}

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

    case DSP_RESET:
    case DSP_FLUSH:
        flush_filters();
        break;

    case DSP_SET_OUT_FREQUENCY:
    case DSP_SET_FREQUENCY:
        exciter_update(dsp, &curr_set);
        break;
    }

    return 0;
}

void dsp_set_exciter(const struct exciter_settings *settings)
{
    struct dsp_config *dsp = dsp_get_config(CODEC_IDX_AUDIO);
    bool enable = exciter_update(dsp, settings);
    dsp_proc_enable(dsp, DSP_PROC_EXCITER, enable);
    dsp_proc_activate(dsp, DSP_PROC_EXCITER, true);
}

DSP_PROC_DB_ENTRY(EXCITER, exciter_configure);
