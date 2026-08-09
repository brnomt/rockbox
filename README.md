# Rockbox Bassboost + Crystalizer

Bass booster (fixed sub-bass gain + saturation), Crystalizer (2-band transient enhancer), Air Exciter, Stereo Widener and Mini Reverb DSP stages for Rockbox — targeting iPod Classic 6G/7G.

## Bassboost

Simple but extremely powerful sub-bass processor designed for maximum impact, utilizing psychoacoustic harmonics to make bass audible on small drivers.

- **Crossover** (40–500 Hz): 4th-order Linkwitz-Riley (-24 dB/octave) for surgical sub-bass isolation.
- **Sub Bass Gain** (0–24 dB, step 0.5, default +12 dB): fixed gain boost applied directly to the sub-bass band.
- **Harmonics** (0–100%): Psychoacoustic harmonic generator based on the MaxxBass principle (full-wave rectification + DC blocking). Creates even-order harmonics that trick the brain into hearing deep bass even if the headphones physically can't reproduce it.
- **Output gain** (±12 dB): Master output trim.
- **Master Soft Clipper (Waveshaper)**: Replaces hard clipping. Sits at the end of the DSP chain and softly rounds peaks that exceed the digital ceiling, preventing harsh digital distortion while maintaining massive volume.

### Defaults

| Parameter | Default |
|-----------|---------|
| Crossover | 80 Hz |
| Sub Bass Gain | +12 dB |
| Harmonics | 0% |
| Output gain | 0 dB |

### Signal flow

```
Input → LR4 LPF@crossover → [Sub Bass Gain] → [Harmonics Gen + DC Block] → ┐
                                                                           ├→ [Soft Clipper] → Output
Input ---------------------------------------------------------------------┘
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

## Air Exciter

Bandlimited even-harmonic generator for the treble — adds "air" and perceived detail without harshness. This is the corrected take on a classic exciter: only the isolated treble band is driven non-linear, so the generated harmonics stay in the treble instead of intermodulating the whole spectrum (the failure mode of a full-band soft-clip exciter).

- **Cutoff** (2000–8000 Hz): LR4 high-pass (-24 dB/octave) isolating the band to be excited.
- **Intensity** (0–100%): amount of generated harmonics mixed back in.

Uses the same MaxxBass-style building blocks as the bassboost Harmonics knob (full-wave rectify + 1-pole DC block), applied to the high-passed signal.

### Signal flow

```
Input → LR4 HPF@cutoff → [Rectify + DC Block] → [× Intensity] → ┐
                                                                 ├→ [Soft Clipper] → Output
Input -----------------------------------------------------------┘
```

## Stereo Widener

Mid/side width control with a tamed low end.

- **Width** (0–200%): scales the side signal. 100% is transparent; 0% is mono; >100% widens.
- **Crossover** (50–500 Hz): side content below this frequency never exceeds unity width, so the bass stays mono-compatible no matter how wide you go.

### Signal flow

```
L,R → M/S decode → side → [LPF@crossover] → low side (≤100% width) ┐
                                          → high side (× width) ────┤→ M/S encode → [Soft Clipper] → Output
```

## Mini Reverb

Compact freeverb-style room simulator: two banks of four damped comb filters with detuned delay lengths (one bank per stereo channel) fed from a mono mix.

- **Room Size** (0–100%): feedback amount — short slap to long hall-like decay.
- **Damping** (0–100%): one-pole low-pass in each feedback loop — how quickly the tail loses treble.
- **Wet Mix** (0–100%): wet/dry blend.

Delay buffers are allocated from the core pool only while the effect is enabled (same pattern as surround), so no RAM is used when it's off.

## Usage

On device: **Settings → Sound Settings → Bassboost / Crystalizer / Air Exciter / Stereo Widener / Reverb**

### Bassboost menu
- **Enable**
- **Crossover** (40–500 Hz, step 10)
- **Sub Bass Gain** (0–24 dB, step 0.5) — Gain added to the sub-bass frequencies.
- **Harmonics** (0–100%, step 5) — Psychoacoustic upper harmonics mix to enhance perceived bass on small speakers.
- **Output Gain** (±12 dB, step 0.5)

### Crystalizer menu
- **Enable**
- **Intensity Mid** (−24 to +24 dB, step 0.1)
- **Intensity High** (−24 to +24 dB, step 0.1)
- **Mix** (0–100%, step 1)
- **Output Gain** (±12 dB, step 0.1)

### Air Exciter menu
- **Enable**
- **Cutoff** (2000–8000 Hz, step 100)
- **Intensity** (0–100%, step 5)

### Stereo Widener menu
- **Enable**
- **Width** (0–200%, step 5)
- **Crossover** (50–500 Hz, step 10)

### Reverb menu
- **Enable**
- **Room Size** (0–100%, step 5)
- **Damping** (0–100%, step 5)
- **Wet Mix** (0–100%, step 5)

### Recommended settings for sub-bass on small drivers

```
Bassboost:
  Enable: ON
  Crossover: 80 Hz
  Sub Bass Gain: +12 dB
  Harmonics: 40%
  Output Gain: 0 dB
```

### Recommended starting points for the new effects

```
Air Exciter:      Enable ON, Cutoff 3500 Hz, Intensity 30%
Stereo Widener:   Enable ON, Width 120%, Crossover 150 Hz
Reverb:           Enable ON, Room Size 50%, Damping 50%, Wet Mix 30%
```

## Files changed vs upstream

```
apps/lang/english.lang              — All menu strings
apps/menus/sound_menu.c             — Bassboost + Crystalizer submenus
apps/settings.c                     — dsp_set_bassboost() / dsp_set_crystalizer()
apps/settings.h                     — bassboost_settings + crystalizer_settings
apps/settings_list.c                — All setting entries + callbacks
apps/onplay.c                       — Go to Album item in WPS context menu
apps/tagtree.c/.h                   — tagtree_goto_album() (Database jump for Go to Album)
apps/root_menu.c, tree.c, gui/wps.c — Go to Album navigation wiring
apps/timed_brightness.c/.h          — Time-based auto brightness controller
apps/menus/display_menu.c           — Timed Brightness submenu
lib/rbcodec/SOURCES                 — bassboost.c + crystalizer.c + exciter.c + widener.c + reverb.c
lib/rbcodec/dsp/dsp_proc_database.h — BASSBOOST + CRYSTALIZER + EXCITER + WIDENER + REVERB registered
lib/rbcodec/dsp/dsp_proc_settings.h — Includes all effect headers
lib/rbcodec/dsp/bassboost.c/.h      — Sub-bass isolator + Psychoacoustic Harmonics + Soft Clipper
lib/rbcodec/dsp/crystalizer.c/.h    — 2-band transient enhancer with mix
lib/rbcodec/dsp/exciter.c/.h        — Bandlimited treble even-harmonic generator
lib/rbcodec/dsp/widener.c/.h        — Mid/side stereo widener with mono bass
lib/rbcodec/dsp/reverb.c/.h         — freeverb-style damped comb reverb
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

- **Master Soft Clipper**: Dynamic waveshaper based on `soft_over = headroom - (headroom * headroom) / (headroom + over)`. Threshold and ceiling track the format's real full scale (`2^frac_bits`, i.e. `2^27` for 16-bit sources), so boosted peaks are soft-limited instead of hard-clipping at the output conversion. Every gain-adding stage (bassboost, crystalizer, exciter, widener, reverb) applies it.
- **Psychoacoustic Harmonics**: Full-wave rectification `abs(x)` followed by a 1-pole DC blocker to generate even-order upper harmonics.
- **Crystalizer 2-band**: LR2 series — LP@60 → LP@3000 = band 0, remainder = band 1
- **Reverb**: freeverb comb tunings scaled by output rate; feedback set by Room Size, one-pole damped loop; delay buffers `core_alloc`'d only while enabled.

## QoL features

### Go to Album (WPS context menu)

New item in the WPS context menu (Select button on iPod while playing):

- **Go to Album** — jumps straight to the current track's album in the Database browser
- Uses tagcache: locates the `"same"` menu (`%menu_start "same"` in `tagnavi.config`), finds its `Album` entry and opens it with the playing track's album preselected
- Falls back to the track's folder in the file browser when the Database isn't available (tagcache not ready or track has no album tag)

Implementation: `apps/onplay.c` — `go_to_album()` + `go_to_album_item`; `apps/tagtree.c` — `tagtree_goto_album()`; wired through `apps/root_menu.c`, `apps/tree.c` and `apps/gui/wps.c`.

### Time-based Auto Brightness

Scheduled day/night backlight levels under **Settings → Display Settings → Timed Brightness** (targets with adjustable backlight brightness and an RTC, e.g. iPod 6G):

- **Enable** (default off) — turning it off restores the manual brightness setting
- **Day Time** (default 07:00) / **Night Time** (default 23:00) — set via the shared time picker screen, same UI as the alarm
- **Day Brightness** (default max, 63 on iPod 6G) / **Night Brightness** (default min, 1)

The controller applies whichever slot is currently active (the one whose trigger time most recently passed) and arms a single self-rearming timeout for the next transition — no per-minute polling, and the callback is ISR-safe. If the RTC has no valid time yet it falls back to manual brightness and retries when settings are applied.

Implementation: `apps/timed_brightness.c/.h`, menu items in `apps/menus/display_menu.c`, settings in `apps/settings_list.c`.

## Inline Earphone Remote (iPod 6G)

Play/pause and volume buttons on Apple inline earphones (headphone jack remote) are decoded via the "Mikey" I2C controller (bus 0, address 0x72):

- **Center button** — play/pause/resume on every screen (reported as a multimedia key, like the OF)
- **Volume +/−** — volume up/down, also works in menus and the file browser
- Keeps working with the hold switch on, like the OF

**Requirements:** a 6G unit with the "Mikey" chip (late-2008/2009 120/160GB models; the early-2007 80/160GB models lack it — check `Settings → Debug → View HW Info`, line `mikey remote ctrl` must show `ok` with headphones plugged in), and Apple-protocol earphones for the volume buttons (center play/pause may also trigger on remotes that short the mic line).

Credit: [Hemant Kumar](https://github.com/hemant6488) — [hemant6488/rockbox, ipod6g-mikey-v1](https://github.com/hemant6488/rockbox/releases/tag/ipod6g-mikey-v1); the patch was merged into official Rockbox upstream (commit `b217a55059`) and is included in this fork.
