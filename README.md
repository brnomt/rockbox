# Rockbox MBC3Band

3-band multiband OTT compressor DSP stage for Rockbox — targeting iPod Classic 6G/7G.

## What it does

Splits audio into 1, 2, or 3 frequency bands (Linkwitz-Riley 12dB/oct crossovers) and applies per-band **downward + upward compression** (OTT-style):

- **Below threshold** → upward compression boosts quiet signals (brings up detail)
- **Above threshold** → downward compression squashes peaks (controls loudness)
- **Soft knee** smooths the transition

## Signal flow

```
3-band: Input → [HPF xover1] → Band 2 (High) ─┐
              → [LPF xover1] → [HPF xover2] → Band 1 (Mid)  ├→ Out
                             → [LPF xover2] → Band 0 (Low) ─┘

1-band: Band 1 (Mid) acts on full-range signal (no crossover)
```

## Usage

On device: **Settings → Sound Settings → Multiband Comp**

- **Enable** the plugin
- **Band Mode** — 1 / 2 / 3 band
- **Band 0/1/2** submenus — threshold, down/up ratio, attack, release, wet mix
- **Crossover Low / High** — Hz, only relevant in 2/3-band modes
- **Output Gain** — ±12 dB global trim

## What's changed vs upstream Rockbox

```
apps/lang/english.lang              — Language strings (Multiband Comp menu)
apps/menus/sound_menu.c             — 3 band submenus + main menu in Sound Settings
apps/settings.c                     — Calls dsp_set_mbc3band() on boot
apps/settings.h                     — Added mbc3band_settings to user_settings
apps/settings_list.c                — Per-band settings entries (threshold, ratio, attack, release, mix)
lib/rbcodec/SOURCES                 — Added dsp/mbc3band.c
lib/rbcodec/dsp/dsp_proc_database.h — Registered MBC3BAND DSP stage after compressor
lib/rbcodec/dsp/dsp_proc_settings.h — Included mbc3band.h
lib/rbcodec/dsp/mbc3band.c          — Full DSP implementation (crossover biquads, gain tables, process loop)
lib/rbcodec/dsp/mbc3band.h          — Settings struct and public API
```

## Defaults

| Band | Freq Range | Threshold | Down Ratio | Up Ratio | Attack | Release |
|------|-----------|-----------|-----------|----------|--------|---------|
| 0 Low | 20–120 Hz | -12 dB | 2:1 | off | 10 ms | 100 ms |
| 1 Mid | 120–2000 Hz | -18 dB | 2:1 | 0.5:1 | 10 ms | 100 ms |
| 2 High | 2000–20000 Hz | -12 dB | 2:1 | 0.5:1 | 10 ms | 100 ms |

## Build

```bash
mkdir build-ipod6g && cd build-ipod6g
../tools/configure --target=29 --type=n
make -j$(nproc)
```

Output: `rockbox.ipod` → copy to `.rockbox/` on your iPod Classic (emCORE bootloader).

## Fixed-point implementation

All DSP math is **fixed-point integer** (S7.24 / S15.16 / Q31) targeting ARM926EJ-S (no FPU). The crossover biquads, gain tables, envelope followers, and wet/dry blending all use the same `FRACMUL` / `fp_factor` / `fp_sincos` machinery as the built-in Rockbox compressor and EQ.
