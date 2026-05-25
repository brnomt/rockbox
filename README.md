# Rockbox Bassboost + Crystalizer (formerly mbc3band)

Bass booster (upward + downward compression with saturation) and Crystalizer (EasyEffects-style multiband transient enhancer) DSP stages for Rockbox — targeting iPod Classic 6G/7G.

## Bassboost

Single-band bass processor with:

- **Upward compression** (expansion below threshold): brings up quiet bass details (ratio: off / 0.25:1 / 0.5:1 / 0.75:1)
- **Downward compression** (attenuation above threshold): controls peaks (ratio: off / 2:1 / 4:1 / 6:1 / 10:1 / limit)
- **Adjustable knee** (0–12 dB) for smooth transition around threshold
- **Pre-gain** (0–24 dB) to drive the compressor harder
- **Drive / saturation** (0–100): cubic soft clip `(3x−x³)/2` on the bass band, up to 4× input scaling
- **Dual envelope follower**: separate fast-attack paths for up/down movement, independent attack (5–50 ms) and release (50–1000 ms)
- **Mix** (0–100%) wet/dry blend

### Aggressive defaults

| Parameter | Default |
|-----------|---------|
| Threshold | -24 dB |
| Down ratio | 4:1 |
| Up ratio | 0.5:1 |
| Attack | 5 ms |
| Release | 150 ms |
| Knee | 6 dB |
| Pre-gain | 0 dB |
| Drive | 0 |
| Makeup gain | +6 dB |
| Mix | 100% |

### Signal flow

```
Input → Pre-gain → [Envelope Follower (up+down)] → [Gain computer (knee)] → [Saturation] → Makeup → Mix → Output
                     ↓
                  Threshold
```

## Crystalizer

EasyEffects-style 3-band transient enhancer:

- **3-band Linkwitz-Riley 12 dB/oct crossover** at 300 Hz and 3000 Hz
- **Second-derivative peak detection** per band (backward difference: `d²[n] = x[n] − 2·x[n−1] + x[n−2]`)
- **Enhancement formula**: `output = band + intensity × d²` (no pre-ringing)
- **Per-band intensity** (−24 to +24 dB), independent control for Low/Mid/High bands
- **Output gain** (−12 to +12 dB)
- **Bands sum to original** when all intensities at 0 dB (perfect reconstruction)

### Signal flow

```
Input → [LPF@300]  → Band 0 (Low)  → enhancer → ┐
       → [HPF@300] → [LPF@3000] → Band 1 (Mid) → enhancer → ├→ Out
                   → [HPF@3000] → Band 2 (High) → enhancer ─┘
```

## Usage

On device: **Settings → Sound Settings → Bassboost / Crystalizer**

### Bassboost menu
- **Enable** — on/off
- **Threshold** — −36 to 0 dB
- **Down Ratio** — off / 2:1 / 4:1 / 6:1 / 10:1 / limit
- **Up Ratio** — off / 0.25:1 / 0.5:1 / 0.75:1
- **Attack** — 5 to 50 ms
- **Release** — 50 to 1000 ms
- **Knee** — 0 to 12 dB
- **Pre-gain** — 0 to 24 dB
- **Drive** — 0 to 100 (saturation intensity)
- **Makeup** — 0 to 12 dB (fixed, not auto)
- **Mix** — 0 to 100%

### Crystalizer menu
- **Enable** — on/off
- **Low intensity** — −24 to +24 dB
- **Mid intensity** — −24 to +24 dB
- **High intensity** — −24 to +24 dB
- **Output gain** — −12 to +12 dB

## What's changed vs upstream Rockbox

```
apps/lang/english.lang              — Language strings (bassboost + crystalizer menus)
apps/menus/sound_menu.c             — Bassboost submenu + Crystalizer submenu in Sound Settings
apps/settings.c                     — Calls dsp_set_bassboost() and dsp_set_crystalizer()
apps/settings.h                     — Added bassboost_settings and crystalizer_settings
apps/settings_list.c                — All bassboost + crystalizer setting entries
lib/rbcodec/SOURCES                 — Added dsp/crystalizer.c
lib/rbcodec/dsp/dsp_proc_database.h — Registered BASSBOOST (pre-existing) + CRYSTALIZER
lib/rbcodec/dsp/dsp_proc_settings.h — Included crystalizer.h
lib/rbcodec/dsp/bassboost.c/.h      — Rewritten: upward expansion, pre-gain, knee, drive, dual envelope
lib/rbcodec/dsp/crystalizer.c/.h    — NEW: EasyEffects-style 3-band transient enhancer
```

## Build

```bash
export PATH="/tmp/arm-bins:$PATH"
mkdir build-ipod6g && cd build-ipod6g
../tools/configure
# select: 29 (iPod Classic), N (normal build)
make -j$(nproc)
```

Output: `rockbox.ipod` → copy to `/.rockbox/` on your iPod Classic.

**Note:** After updating the binary, delete `/.rockbox/config.cfg` on the device (or do a settings reset) to avoid "Incompatible Version" errors after struct changes.

## ARM fixed-point implementation

All DSP math is **fixed-point integer** (S7.24 / S15.16 / Q31) targeting ARM926EJ-S (no FPU). The biquad crossovers, gain tables, envelope followers, and saturation all use the same `FRACMUL` / `fp_factor` / `fp_sincos` machinery as the built-in Rockbox compressor and EQ.

### Key design decisions

- **Bassboost gain table** computes combined upward and downward gain in one pass; signals below threshold get gain > unity (boost), signals above get gain < unity (attenuation), with knee interpolation.
- **Dual envelope follower** splits target gain into `target_down = min(total, unity)` and `target_up = max(total, unity)` with independent attack/release rates.
- **Saturation** applies cubic soft clip `(3x−x³)/2` on the bass band only, with input scaled up to 4× before waveshaping.
- **Crystalizer** uses backward difference `d²[n] = x[n] − 2·x[n-1] + x[n-2]` (no lookahead) with `enhanced = band + intensity·d²` to avoid pre-ringing.
- **3-band crossover** uses series LR2 topology: LP@f1 → band 0, HP@f1 → LP@f2 → band 1, remainder = band 2; sum of bands = original input.
