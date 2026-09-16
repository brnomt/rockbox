/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Mini reverb - compact Schroeder/freeverb style room simulator
 * Fixed-point implementation for ARM targets (iPod Classic 6/7)
 *
 * Signal flow:
 *   (L+R)/2 -> 2x4 parallel comb filters with damped feedback
 *            -> one set per stereo channel (detuned delay lengths)
 *            -> wet/dry mix -> Soft limiter
 *
 * Delay lengths follow the classic freeverb tuning (44.1 kHz basis),
 * scaled with the output sample rate. Room size sets the feedback
 * amount (decay length); damping is a one-pole low-pass inside each
 * feedback loop controlling how fast the tail loses its treble.
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
#include "reverb.h"
#include "dsp_proc_entry.h"
#include "dsp_core.h"
#include "dsp_filter.h"
#include "dsp_sample_io.h"
#include "dsp_misc.h"
#include "core_alloc.h"
#include "platform.h"

#define UNITY            (1L << 24)
#define NUM_LINES        8
#define LINES_PER_SIDE   4
/* Largest tuned length scaled to the target's maximum output rate (48 kHz
 * on iPod 6G -> 48 KB instead of 96 KB); reverb_update_lengths() clamps to
 * this so an unexpected rate shortens the room instead of overflowing. */
#define BASE_RATE        44100
#define MAX_LEN          (1379 * DSP_OUT_MAX_HZ / BASE_RATE + 1)

/* Freeverb-style comb tunings at 44.1 kHz: first four feed the left
 * output, last four the right (detuned for stereo spread). */
static const int base_lengths[NUM_LINES] =
{
    1116, 1188, 1277, 1356,
    1139, 1211, 1300, 1379
};

static struct reverb_settings curr_set;

/* Comb indices/state and gains in IRAM — touched 8× per sample. */
static int lengths[NUM_LINES] IBSS_ATTR;
static int pos[NUM_LINES] IBSS_ATTR;
static int32_t lp_state[NUM_LINES] IBSS_ATTR;

static int32_t feedback IBSS_ATTR = UNITY / 2;
static int32_t damp1 IBSS_ATTR, damp2 IBSS_ATTR;
static int32_t wet_gain IBSS_ATTR;

static int handle = -1;

#define REVERB_BUFSIZE (NUM_LINES * MAX_LEN * sizeof (int32_t))

static int reverb_buffer_alloc(void)
{
    handle = core_alloc(REVERB_BUFSIZE);
    return handle;
}

static void reverb_buffer_free(void)
{
    if (handle < 0)
        return;
    core_free(handle);
    handle = -1;
}

static void reverb_flush(void)
{
    if (handle >= 0)
        memset(core_get_data(handle), 0, REVERB_BUFSIZE);
    memset(pos, 0, sizeof(pos));
    memset(lp_state, 0, sizeof(lp_state));
}

/* ------------------------------------------------------------------ */
/*  One comb line step                                                */
/* ------------------------------------------------------------------ */
static FORCE_INLINE int32_t comb_step(int32_t *buf, int line, int32_t input)
{
    int p = pos[line];
    int32_t out = buf[p];

    /* Damping: one-pole low-pass in the feedback loop */
    int64_t filt = ((int64_t)out * damp2 + (int64_t)lp_state[line] * damp1) >> 24;
    lp_state[line] = (int32_t)filt;

    /* Resonant gain reaches 1/(1-0.95) = 20x at room 100; saturate the
     * stored state rather than let it wrap into a recirculating burst. */
    int64_t nv = (int64_t)input + ((filt * feedback) >> 24);
    if (nv > INT32_MAX)      nv = INT32_MAX;
    else if (nv < INT32_MIN) nv = INT32_MIN;
    buf[p] = (int32_t)nv;

    if (++p >= lengths[line])
        p = 0;
    pos[line] = p;

    return out;
}

/* ------------------------------------------------------------------ */
/*  Main processing callback                                          */
/* ------------------------------------------------------------------ */
static void reverb_process(struct dsp_proc_entry *this,
                           struct dsp_buffer **buf_p)
{
    (void)this;
    struct dsp_buffer *buf = *buf_p;
    int count      = buf->remcount;
    int32_t *out0  = buf->p32[0];
    int32_t *out1  = buf->p32[1];
    const int num_chan = buf->format.num_channels;

    int32_t *lines = core_get_data(handle);
    if (lines == NULL)
        return;

    /* Full scale is 2^frac_bits (27 for 16-bit sources). */
    const int64_t max_val = ((int64_t)1 << buf->format.frac_bits) - 1;
    const int32_t wgain = wet_gain;

    /* Line bases once per buffer — avoids i*MAX_LEN every sample. */
    int32_t *linep[NUM_LINES];
    for (int i = 0; i < NUM_LINES; i++)
        linep[i] = lines + i * MAX_LEN;

    for (int n = 0; n < count; n++)
    {
        int32_t L = out0[n];
        int32_t R = (num_chan > 1) ? out1[n] : L;

        /* Mono drive signal, attenuated for feedback-loop headroom: with
         * 28/29 frac-bit sources (MP3, FLAC, WMA) a >>2 drive at 20x comb
         * gain exceeds int32; >>4 and summing (not averaging) the four lines
         * per side gives the same wet level with 2 more bits of margin. */
        int32_t input = (int32_t)(((int64_t)L + R) >> 4);

        int64_t wetL = 0, wetR = 0;

        for (int i = 0; i < LINES_PER_SIDE; i++)
            wetL += comb_step(linep[i], i, input);
        for (int i = LINES_PER_SIDE; i < NUM_LINES; i++)
            wetR += comb_step(linep[i], i, input);

        if (wgain != UNITY)
        {
            wetL = (wetL * wgain) >> 24;
            wetR = (wetR * wgain) >> 24;
        }

        /* Clamp in 64-bit before narrowing; peak control belongs to the
         * compressor stage (no per-sample divide, no knee below FS). */
        int64_t newL = (int64_t)L + wetL;
        if (newL > max_val)       newL = max_val;
        else if (newL < -max_val) newL = -max_val;
        out0[n] = (int32_t)newL;

        if (num_chan > 1)
        {
            int64_t newR = (int64_t)R + wetR;
            if (newR > max_val)       newR = max_val;
            else if (newR < -max_val) newR = -max_val;
            out1[n] = (int32_t)newR;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Update from settings                                              */
/* ------------------------------------------------------------------ */
static void reverb_update_lengths(unsigned long fs)
{
    for (int i = 0; i < NUM_LINES; i++)
    {
        int len = (int)(((int64_t)base_lengths[i] * fs) / BASE_RATE);
        if (len < 1)
            len = 1;
        else if (len > MAX_LEN)
            len = MAX_LEN;
        lengths[i] = len;
        if (pos[i] >= len)
            pos[i] = 0;
    }
}

static bool reverb_update(struct dsp_config *dsp,
                          const struct reverb_settings *settings)
{
    /* No wet signal means pass-through: don't run (or allocate) at all. */
    if (!settings->enabled || settings->wet_mix <= 0)
        return false;

    unsigned long fs = dsp_get_output_frequency(dsp);
    if (fs <= 0)
        return false;

    curr_set = *settings;

    reverb_update_lengths(fs);

    int room = settings->room_size;
    if (room < 0) room = 0;
    if (room > 100) room = 100;
    /* Feedback 0.60..0.95 -> short slap to long hall-ish decay */
    feedback = (UNITY / 100) * (60 + (room * 35) / 100);

    int damp = settings->damping;
    if (damp < 0) damp = 0;
    if (damp > 100) damp = 100;
    /* Up to 0.9 of the loop energy recirculated through the low-pass */
    damp1 = (int32_t)(((int64_t)UNITY * damp * 9) / 1000);
    damp2 = UNITY - damp1;

    int mix = settings->wet_mix;
    if (mix < 0) mix = 0;
    if (mix > 100) mix = 100;
    wet_gain = ((int64_t)mix * UNITY) / 100;

    return true;
}

/* ------------------------------------------------------------------ */
/*  DSP configuration hook                                            */
/* ------------------------------------------------------------------ */
static intptr_t reverb_configure(struct dsp_proc_entry *this,
                                 struct dsp_config *dsp,
                                 unsigned int setting,
                                 intptr_t value)
{
    intptr_t retval = 0;

    switch (setting)
    {
    case DSP_PROC_INIT:
        if (value != 0)
            break;
        retval = reverb_buffer_alloc();
        if (retval < 0)
            break;
        /* Settings/lengths were just computed by dsp_set_reverb(). */
        this->process = reverb_process;
        reverb_flush();
        retval = 0;
        break;

    case DSP_PROC_CLOSE:
        reverb_buffer_free();
        break;

    case DSP_RESET:
    case DSP_FLUSH:
        reverb_flush();
        break;

    /* Only the output rate matters (resampler runs before this stage). */
    case DSP_SET_OUT_FREQUENCY:
        reverb_update(dsp, &curr_set);
        break;
    }

    return retval;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
void dsp_set_reverb(const struct reverb_settings *settings)
{
    struct dsp_config *dsp = dsp_get_config(CODEC_IDX_AUDIO);
    bool enable = reverb_update(dsp, settings);
    dsp_proc_enable(dsp, DSP_PROC_REVERB, enable);
    dsp_proc_activate(dsp, DSP_PROC_REVERB, true);
}

DSP_PROC_DB_ENTRY(REVERB, reverb_configure);
