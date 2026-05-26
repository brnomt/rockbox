# Rockbox Bassboost + Crystalizer

Bass booster (upward + downward compression, saturation, sub-octave synthesis) and Crystalizer (2-band transient enhancer) DSP stages for Rockbox — targeting iPod Classic 6G/7G.

## Bassboost

Single-band sub-bass processor with:

- **Upward expansion** (boost below threshold): 0.10:1 / 0.15:1 / 0.25:1 / 0.5:1 / 0.75:1
- **Downward compression** (attenuate above threshold): 2:1 / 4:1 / 6:1 / 10:1 / limit
- **Adjustable knee** (0–12 dB) for smooth transition
- **Pre-gain** (0–24 dB) to drive the compressor
- **Drive / saturation** (0–100%): cubic soft clip `(3x−x³)/2`, up to 4× input scaling
- **Dual envelope follower**: independent attack/release for up/down
- **Makeup gain** (0–24 dB) post-compression
- **Mix** (0–100%) wet/dry blend
- **Harmonic spread** (0–100%): injects saturated bass harmonics into full-band output (psychoacoustic "missing fundamental" effect — the brain fills in deep bass from harmonics reaching the mids)
- **Sub Octave** (0–100%): sub-harmonic octaver via zero-crossing detection — flips polarity at each zero-cross to halve effective frequency, then LPF at 40 Hz. Synthesizes bass one octave below the input
- **Output gain** (±12 dB)

### Defaults

| Parameter | Default |
|-----------|---------|
| Crossover | 120 Hz |
| Threshold | -18 dB |
| Down ratio | 4:1 |
| Up ratio | 0.5:1 |
| Attack | 5 ms |
| Release | 150 ms |
| Knee | 6 dB |
| Pre-gain | 0 dB |
| Drive | 0% |
| Makeup gain | +6 dB |
| Mix | 100% |
| Spread | 0% |
| Sub Octave | 0% |
| Output gain | 0 dB |

### Signal flow

```
Input → LR2 LPF@crossover → [Drive sat] → [Pre-gain] → [Envelope (up+down)] → [Gain table (knee)] → Makeup → Mix → Output
                                  ↓                            ↑
                              [Spread] ────────────────────────→ (full-band)
                              [Sub Octave] → LPF@40Hz ─────────→ (full-band)
```

## Crystalizer

2-band transient enhancer:

- **2-band Linkwitz-Riley 12 dB/oct crossover** at 60 Hz and 3000 Hz
- **Second-derivative peak detection** per band: `d²[n] = x[n] − 2·x[n-1] + x[n-2]`
- **Enhancement**: `output = band + intensity × d²` (no pre-ringing)
- **Intensity Mid** (−24 to +24 dB) for 60–3000 Hz
- **Intensity High** (−24 to +24 dB) for 3000 Hz+
- **Mix** (0–100%) wet/dry blend
- **Output gain** (±12 dB)
- **0.1 dB granularity** on all controls

### Signal flow

```
Input → [LPF@60] → [LPF@3000] → Band Mid (60-3000) → enhancer → ┐
                 → [HPF@3000] → Band High (3000+)  → enhancer → ├→ Mix → Out
```

## Bug fixes (bassboost)

- **get_gain mute**: returned -1 (silence) for signals > +12 dBFS. Fixed to return `gain_table[65]`.
- **Interpolation precision**: 0→+12 dB region lost ~90% resolution due to integer division before scaling. Fixed with int64 division.
- **Static init**: `attca`, `rlsca`, `makeup_gain`, `pre_gain_linear`, `drive_scale`, `output_gain`, `dry_mix` initialized to UNITY as safe defaults.

## Usage

On device: **Settings → Sound Settings → Bassboost / Crystalizer**

### Bassboost menu
- **Enable**
- **Crossover** (40–500 Hz)
- **Threshold** (−60 to 0 dB, step 3)
- **Down Ratio** (off / 2:1 / 4:1 / 6:1 / 10:1 / limit)
- **Up Ratio** (off / 0.10:1 / 0.15:1 / 0.25:1 / 0.5:1 / 0.75:1)
- **Pre-gain** (0–24 dB, step 0.5)
- **Knee** (0–12 dB, step 1)
- **Drive** (0–100%, step 5)
- **Attack** (1–500 ms, step 5)
- **Release** (10–2000 ms, step 10)
- **Makeup** (0–24 dB, step 0.5)
- **Mix** (0–100%, step 5)
- **Harmonic Spread** (0–100%, step 5)
- **Sub Octave** (0–100%, step 5)
- **Output Gain** (±12 dB, step 0.5)

### Crystalizer menu
- **Enable**
- **Intensity Mid** (−24 to +24 dB, step 0.1)
- **Intensity High** (−24 to +24 dB, step 0.1)
- **Mix** (0–100%, step 1)
- **Output Gain** (±12 dB, step 0.1)

### Recommended settings for sub-bass on small drivers

```
Bassboost:
  Enable: ON
  Crossover: 80 Hz
  Threshold: -24 dB
  Down Ratio: 4:1
  Up Ratio: 0.5:1
  Pre-gain: +6 dB
  Knee: 6 dB
  Drive: 60%
  Attack: 5 ms
  Release: 100 ms
  Makeup: +10 dB
  Mix: 100%
  Harmonic Spread: 40%
  Sub Octave: 30%
  Output Gain: 0 dB
```

## Files changed vs upstream

```
apps/lang/english.lang              — All menu strings
apps/menus/sound_menu.c             — Bassboost + Crystalizer submenus
apps/settings.c                     — dsp_set_bassboost() / dsp_set_crystalizer()
apps/settings.h                     — bassboost_settings + crystalizer_settings
apps/settings_list.c                — All setting entries + callbacks
lib/rbcodec/SOURCES                 — bassboost.c + crystalizer.c
lib/rbcodec/dsp/dsp_proc_database.h — BASSBOOST + CRYSTALIZER registered
lib/rbcodec/dsp/dsp_proc_settings.h — Includes both headers
lib/rbcodec/dsp/bassboost.c/.h      — Rewritten: upward comp, knee, drive, dual envelope, spread, sub_octave
lib/rbcodec/dsp/crystalizer.c/.h    — 2-band transient enhancer with mix
```

## Build

```bash
export PATH="/tmp/arm-bins:$PATH"
mkdir build-ipod6g && cd build-ipod6g
../tools/configure
# select: 29 (iPod Classic), N (normal build)
make -j$(nproc)
```

Copy `rockbox.ipod` to `/.rockbox/` on the iPod. Also copy `build-ipod6g/apps/lang/english.lng` to `/.rockbox/langs/`.

**Note:** After structural changes, delete `/.rockbox/config.cfg` or reset settings to avoid "Incompatible Version" errors.

## ARM fixed-point notes

All DSP math is **fixed-point integer** (S7.24 / S15.16 / Q31) targeting ARM926EJ-S. Biquads, gain tables, envelope followers, and saturation use `FRACMUL` / `fp_factor` / `fp_sincos`.

- **Gain table** (66 entries): combined upward + downward + knee in one pass
- **Dual envelope**: `target_down = min(total, unity)`, `target_up = max(total, unity)` with independent attack/release
- **Cubic soft-clip**: `(3x−x³)/2`, input scaled up to 4×
- **Sub Octave**: zero-crossing octaver — toggles polarity at each sign flip, then LR2 LPF@40Hz
- **Crystalizer 2-band**: LR2 series — LP@60 → LP@3000 = band 0, remainder = band 1
