# Rockbox Bassboost + Crystalizer

Bass booster (fixed sub-bass gain + saturation) and Crystalizer (2-band transient enhancer) DSP stages for Rockbox — targeting iPod Classic 6G/7G.

## Bassboost

Simple sub-bass processor designed for maximum impact with minimal controls:

- **Sub Bass Gain** (0–24 dB, step 0.5, default +12 dB): fixed gain boost applied directly to the low-passed bass band (no compressor — this is a straight multiplicative boost that makes sub-bass hit HARD)
- **Drive / saturation** (0–100%): cubic soft clip `(3x−x³)/2`, up to 4× input scaling. Adds harmonics for perceived bass
- **Mix** (0–100%) wet/dry blend
- **Output gain** (±12 dB)
- **Hard limiter** at 0 dBFS on the boosted bass band prevents overflow

### Defaults

| Parameter | Default |
|-----------|---------|
| Crossover | 80 Hz |
| Sub Bass Gain | +12 dB |
| Drive | 0% |
| Mix | 100% |
| Output gain | 0 dB |

### Signal flow

```
Input → LR2 LPF@crossover → [Drive sat] → [Sub Bass Gain] → [Hard limiter] → Mix → Output
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

## Usage

On device: **Settings → Sound Settings → Bassboost / Crystalizer**

### Bassboost menu
- **Enable**
- **Crossover** (40–500 Hz, step 10)
- **Sub Bass Gain** (0–24 dB, step 0.5) — fixed boost to the bass band. Default +12 dB
- **Drive** (0–100%, step 5) — saturation adds harmonics for perceived bass
- **Mix** (0–100%, step 5) — wet/dry blend
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
  Sub Bass Gain: +12 dB
  Drive: 60%
  Mix: 100%
  Output Gain: 0 dB
```

## Files changed vs upstream

```
apps/lang/english.lang              — All menu strings
apps/menus/sound_menu.c             — Bassboost + Crystalizer submenus
apps/settings.c                     — dsp_set_bassboost() / dsp_set_crystalizer()
apps/settings.h                     — bassboost_settings + crystalizer_settings
apps/settings_list.c                — All setting entries + callbacks
apps/onplay.c                       — Go to Album item in WPS context menu
lib/rbcodec/SOURCES                 — bassboost.c + crystalizer.c
lib/rbcodec/dsp/dsp_proc_database.h — BASSBOOST + CRYSTALIZER registered
lib/rbcodec/dsp/dsp_proc_settings.h — Includes both headers
lib/rbcodec/dsp/bassboost.c/.h      — Simplified: fixed sub-bass gain + drive + hard limiter
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

- **Cubic soft-clip**: `(3x−x³)/2`, input scaled up to 4×
- **Hard limiter**: clips boosted bass at ±1.0 (0 dBFS) to prevent overflow
- **Crystalizer 2-band**: LR2 series — LP@60 → LP@3000 = band 0, remainder = band 1

## WPS Context Menu — Go to Album

New item in the WPS context menu (Select button on iPod while playing):

- **Go to Album** — navigates directly to the current track's folder in the file browser
- Uses `audio_current_track()->path` to locate the file
- Skips the ID3 info screen, jumps straight to directory browsing

Implementation: `apps/onplay.c` — `go_to_album()` function + `go_to_album_item` MENUITEM_FUNCTION
