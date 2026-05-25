/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Bass booster - fixed-point implementation
 *   Input → LR2 LPF → Drive → Compress (up+down) → Mix → Output
 *
 * Features:
 *  - LR2 Butterworth crossover
 *  - Drive/saturation (cubic soft clip)
 *  - Pre-gain before envelope detector
 *  - Combined gain table: upward + downward + knee
 *  - Dual envelope follower
 *  - Wet/dry mix, makeup gain, output gain
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
#define GAIN_TABLE_SIZE 66
#define LIMIT_PCT 10000

static const int32_t db_table[64] = {
    0x000000, 0x241FA4, 0x1E1A5E, 0x1A94C8,
    0x181518, 0x1624EA, 0x148F82, 0x1338BD,
    0x120FD2, 0x1109EB, 0x101FA4, 0x0F4BB6,
    0x0E8A3C, 0x0DD840, 0x0D3377, 0x0C9A0E,
    0x0C0A8C, 0x0B83BE, 0x0B04A5, 0x0A8C6C,
    0x0A1A5E, 0x09ADE1, 0x094670, 0x08E398,
    0x0884F6, 0x082A30, 0x07D2FA, 0x077F0F,
    0x072E31, 0x06E02A, 0x0694C8, 0x064BDF,
    0x060546, 0x05C0DA, 0x057E78, 0x053E03,
    0x04FF5F, 0x04C273, 0x048726, 0x044D64,
    0x041518, 0x03DE30, 0x03A89B, 0x037448,
    0x03412A, 0x030F32, 0x02DE52, 0x02AE80,
    0x027FB0, 0x0251D6, 0x0224EA, 0x01F8E2,
    0x01CDB4, 0x01A359, 0x0179C9, 0x0150FC,
    0x0128EB, 0x010190, 0x00DAE4, 0x00B4E1,
    0x008F82, 0x006AC1, 0x004699, 0x002305
};

static struct bassboost_settings curr_set;
static int32_t output_gain = UNITY;
static int32_t gain_table[GAIN_TABLE_SIZE];
static int32_t attca = UNITY, attcb, rlsca = UNITY, rlscb;
static int32_t makeup_gain = UNITY;
static int32_t pre_gain_linear = UNITY;
static int32_t wet_mix, dry_mix = UNITY;
static int32_t drive_scale = UNITY, drive_blend;
static int32_t envelope_down[MAX_CH], envelope_up[MAX_CH];
static struct dsp_filter lpf[2];

static const int ratio_down_map[6] = {0, 200, 400, 600, 1000, LIMIT_PCT};
static const int ratio_up_map[6] = {0, 10, 15, 25, 50, 75};

static int32_t get_tc_coeff(int32_t rc_ms, int32_t fs)
{
    if (rc_ms <= 0)
        return UNITY;
    int32_t c = UNITY / fs;
    c *= 1152;
    c /= rc_ms;
    return c;
}

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

static void setup_lpf(int cutoff_hz, unsigned long fs)
{
    unsigned long phase = fp_div(cutoff_hz, fs, 32);
    butterworth_coefs(phase, false, &lpf[0]);
    butterworth_coefs(phase, false, &lpf[1]);
}

static void flush_lpf(void)
{
    filter_flush(&lpf[0]);
    filter_flush(&lpf[1]);
}

static void build_gain_table(int threshold, int ratio_down_pct, int ratio_up_pct, int knee_db)
{
    int32_t thresh = (int32_t)threshold << 16;
    int32_t knee_half  = (knee_db << 16) >> 1;

    for (int i = 0; i < 64; i++)
    {
        int32_t this_db;
        if (i == 0)
            this_db = -db_table[1];
        else
            this_db = -db_table[i];
        int32_t gain_db = 0;

        int32_t knee_bottom = thresh - knee_half;
        int32_t knee_top    = thresh + knee_half;

        if (knee_db <= 0)
        {
            knee_bottom = thresh;
            knee_top    = thresh;
        }

        if (this_db >= knee_top)
        {
            if (ratio_down_pct == LIMIT_PCT)
                gain_db = thresh - this_db;
            else if (ratio_down_pct > 100)
            {
                int32_t excess = this_db - thresh;
                int32_t reduction =
                    (int32_t)(((int64_t)excess * (ratio_down_pct - 100)) / ratio_down_pct);
                gain_db = -reduction;
            }
        }
        else if (this_db <= knee_bottom)
        {
            if (ratio_up_pct > 0 && ratio_up_pct < 100)
            {
                int32_t deficit = thresh - this_db;
                gain_db = (int32_t)(((int64_t)deficit * (100 - ratio_up_pct)) / 100);
            }
        }
        else
        {
            int32_t g_bottom = 0;
            if (ratio_up_pct > 0 && ratio_up_pct < 100)
            {
                int32_t def = thresh - knee_bottom;
                g_bottom = (int32_t)(((int64_t)def * (100 - ratio_up_pct)) / 100);
            }

            int32_t g_top = 0;
            if (ratio_down_pct == LIMIT_PCT)
                g_top = thresh - knee_top;
            else if (ratio_down_pct > 100)
            {
                int32_t ex = knee_top - thresh;
                g_top = -(int32_t)(((int64_t)ex * (ratio_down_pct - 100)) / ratio_down_pct);
            }

            int32_t frac = this_db - knee_bottom;
            int32_t knee_range = knee_top - knee_bottom;
            if (knee_range > 0)
                gain_db = g_bottom +
                    (int32_t)(((int64_t)(g_top - g_bottom) * frac) / knee_range);
        }

        if (gain_db > 24 << 16)
            gain_db = 24 << 16;
        if (gain_db < -24 << 16)
            gain_db = -24 << 16;

        gain_table[i] = fp_factor(gain_db, 16) << 8;
    }

    {
        int32_t this_db = 0;
        int32_t gain_db = 0;
        if (this_db >= thresh + knee_half)
        {
            if (ratio_down_pct == LIMIT_PCT)
                gain_db = thresh - this_db;
            else if (ratio_down_pct > 100)
            {
                int32_t excess = this_db - thresh;
                if (excess > 0)
                    gain_db = -(int32_t)(((int64_t)excess * (ratio_down_pct - 100)) / ratio_down_pct);
            }
        }
        if (gain_db < -24 << 16)
            gain_db = -24 << 16;
        gain_table[64] = fp_factor(gain_db, 16) << 8;
    }

    {
        int32_t over_12 = 12 << 16;
        int32_t this_db = over_12;
        int32_t gain_db = 0;
        if (ratio_down_pct == LIMIT_PCT)
            gain_db = thresh - this_db;
        else if (ratio_down_pct > 100)
        {
            int32_t excess = this_db - thresh;
            if (excess > 0)
                gain_db = -(int32_t)(((int64_t)excess * (ratio_down_pct - 100)) / ratio_down_pct);
        }
        if (gain_db < -24 << 16)
            gain_db = -24 << 16;
        gain_table[65] = fp_factor(gain_db, 16) << 8;
    }
}

static FORCE_INLINE int32_t get_gain(int32_t sample, int frac_bits)
{
    const int off = frac_bits - 15;
    if (off > 0)
        sample >>= off;
    else if (off < 0)
        sample <<= -off;

    if (sample <= 0)
        return gain_table[0];

    if (sample < (1 << 15))
    {
        int idx = sample >> 9;
        int32_t rem = (sample & 0x1FF) << 22;
        int32_t g0 = gain_table[idx];
        int32_t g1 = gain_table[idx + 1];
        int32_t diff = g0 - g1;
        return g0 - (int32_t)(((int64_t)rem * (int64_t)diff) >> 31);
    }

    if (sample < (1 << 17))
    {
        int32_t frac = (int32_t)(((int64_t)(sample - (1 << 15)) << 16) / 3);
        int32_t g64 = gain_table[64];
        int32_t g65 = gain_table[65];
        int32_t diff = g64 - g65;
        return g64 - (int32_t)(((int64_t)frac * (int64_t)diff) >> 31);
    }

    return gain_table[65];
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

static void bassboost_process(struct dsp_proc_entry *this,
                               struct dsp_buffer **buf_p)
{
    (void)this;
    struct dsp_buffer *buf = *buf_p;
    int count = buf->remcount;
    int32_t *out_l = buf->p32[0];
    int32_t *out_r = buf->p32[1];
    const int num_ch = buf->format.num_channels;
    const int frac_bits = buf->format.frac_bits;

    for (int s = 0; s < count; s++)
    {
        int32_t L = out_l[s];
        int32_t R = (num_ch > 1) ? out_r[s] : L;

        int32_t outL = 0, outR = 0;

        for (int ch = 0; ch < num_ch; ch++)
        {
            int32_t x = (ch == 0) ? L : R;

            int32_t lp = biquad_step(&lpf[0], ch, x);
            lp = biquad_step(&lpf[1], ch, lp);

            int32_t hp = x - lp;

            lp = apply_drive(lp, drive_scale, drive_blend);

            int32_t sidechain = FRACMUL_SHL(lp, pre_gain_linear, 7);
            int32_t abs_lp = (sidechain < 0) ? -(sidechain + 1) : sidechain;
            int32_t target_gain = get_gain(abs_lp, frac_bits);

            int32_t target_down = (target_gain <= UNITY) ? target_gain : UNITY;
            int32_t target_up   = (target_gain >= UNITY) ? target_gain : UNITY;

            if (target_down <= envelope_down[ch])
                envelope_down[ch] = FRACMUL_SHL(envelope_down[ch], attcb, 7)
                                  + FRACMUL_SHL(target_down, attca, 7);
            else
                envelope_down[ch] = FRACMUL_SHL(envelope_down[ch], rlscb, 7)
                                  + FRACMUL_SHL(target_down, rlsca, 7);

            if (target_up >= envelope_up[ch])
                envelope_up[ch] = FRACMUL_SHL(envelope_up[ch], attcb, 7)
                                + FRACMUL_SHL(target_up, attca, 7);
            else
                envelope_up[ch] = FRACMUL_SHL(envelope_up[ch], rlscb, 7)
                                + FRACMUL_SHL(target_up, rlsca, 7);

            int32_t gain = FRACMUL_SHL(envelope_down[ch], envelope_up[ch], 7);
            gain = FRACMUL_SHL(gain, makeup_gain, 7);

            int32_t wet_gain = FRACMUL_SHL(wet_mix, gain, 7);
            int32_t lp_out = FRACMUL_SHL(lp, dry_mix + wet_gain, 7);

            int32_t merged = hp + lp_out;

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

static bool bassboost_update(struct dsp_config *dsp,
                              const struct bassboost_settings *settings)
{
    if (!settings->enabled)
        return false;

    int32_t fs = dsp_get_output_frequency(dsp);
    if (fs <= 0)
        return false;

    curr_set = *settings;

    int rd = settings->ratio_down;
    int ratio_down_val = (rd >= 0 && rd < 6) ? ratio_down_map[rd] : 400;

    int ru = settings->ratio_up;
    int ratio_up_val = (ru >= 0 && ru < 6) ? ratio_up_map[ru] : 50;

    setup_lpf(settings->crossover_hz, fs);
    build_gain_table(settings->threshold, ratio_down_val, ratio_up_val,
                     settings->knee_db);

    int at_ms = settings->attack_ms;
    int rl_ms = settings->release_ms;

    int32_t a = (at_ms > 0) ? get_tc_coeff(at_ms, fs) : UNITY;
    int32_t r = get_tc_coeff(rl_ms, fs);

    attca = a;
    attcb = UNITY - a;
    rlsca = r;
    rlscb = UNITY - r;

    if (settings->pre_gain > 0)
    {
        int32_t pg_db_int = settings->pre_gain / 10;
        int32_t pg_db_frac = settings->pre_gain % 10;
        int32_t pg_s16 = (pg_db_int << 16) + (pg_db_frac * 6554);
        pre_gain_linear = fp_factor(pg_s16, 16) << 8;
    }
    else
    {
        pre_gain_linear = UNITY;
    }

    if (settings->makeup_gain_db > 0)
    {
        int32_t mg_db_int = settings->makeup_gain_db / 10;
        int32_t mg_db_frac = settings->makeup_gain_db % 10;
        int32_t mg_s16 = (mg_db_int << 16) + (mg_db_frac * 6554);
        makeup_gain = fp_factor(mg_s16, 16) << 8;
    }
    else
    {
        makeup_gain = UNITY;
    }

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

    int mix = settings->mix;
    if (mix < 0) mix = 0;
    if (mix > 100) mix = 100;
    wet_mix = ((int64_t)mix * UNITY) / 100;
    dry_mix = UNITY - wet_mix;

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
        for (int ch = 0; ch < MAX_CH; ch++)
        {
            envelope_down[ch] = UNITY;
            envelope_up[ch] = UNITY;
        }
        flush_lpf();
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
