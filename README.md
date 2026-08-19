# 🎸 Rockbox Bassboost + Crystalizer

Bass booster, Crystalizer, Air Exciter, Stereo Widener and Mini Reverb DSP stages for Rockbox — targeting iPod Classic 6G/7G. 🎧

> Sub-bass you feel, not just hear. Psychoacoustic harmonics make bass audible even on small drivers.

## 🔊 Bassboost

A simple but powerful sub-bass processor with psychoacoustic harmonics (MaxxBass-style) to make bass audible on small headphones/drivers.

- **Crossover** (40–500 Hz): 4th-order Linkwitz-Riley (-24 dB/oct) to isolate sub-bass.
- **Sub Bass Gain** (0–24 dB, step 0.5, default +12 dB): fixed gain applied to the sub-bass band.
- **Harmonics** (0–100%): psychoacoustic harmonic generator (full-wave rectification + DC blocking). Creates even-order harmonics that trick the brain into perceiving deep bass.
- **Output gain** (±12 dB): master trim of the processed branch.
- **Peak Limiter**: linked-channel peak limiter on the **wet branch** (instant attack, ~100 ms exponential release). When boosted peaks exceed the ceiling (~ −1.9 dBFS), the bass branch is scaled down linearly instead of flattening the waveform. Because it lives on the wet branch, **it does not duck mids/highs** — the annoying global pumping is gone. Channels share one gain value, so the stereo image never shifts.

### Defaults

| Parameter | Default |
|-----------|---------|
| Crossover | 80 Hz |
| Sub Bass Gain | +12 dB |
| Harmonics | 0% |
| Output gain | 0 dB |

### Signal flow

```
                 ┌→ [Sub Bass Gain] → [Harmonics + DC Block] → [Branch Gain] → [Peak Limiter (wet only)] → ┐
Input → LR4 LPF ┤                                                                                          ├→ (+) → Output
                 └─────────────────────── dry (mids/highs untouched) ─────────────────────────────────────┘
```

## ✨ Crystalizer

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

## 🌬️ Air Exciter

Bandlimited even-harmonic generator for the treble — adds "air" and perceived detail without harshness. Only the isolated treble band is driven non-linear, so the generated harmonics stay in the treble instead of intermodulating the whole spectrum.

- **Cutoff** (2000–8000 Hz): LR4 high-pass (-24 dB/oct) isolating the band to be excited.
- **Intensity** (0–100%): amount of generated harmonics mixed back in.

Uses the same MaxxBass-style building blocks as the bassboost Harmonics knob (full-wave rectify + 1-pole DC block), applied to the high-passed signal.

### Signal flow

```
Input → LR4 HPF@cutoff → [Rectify + DC Block] → [× Intensity] → ┐
                                                                  ├→ [Soft Clipper] → Output
Input ----------------------------------------------------------┘
```

## 🎚️ Stereo Widener

Mid/side width control with a tamed low end.

- **Width** (0–200%): scales the side signal. 100% is transparent; 0% is mono; >100% widens.
- **Crossover** (50–500 Hz): side content below this frequency never exceeds unity width, so the bass stays mono-compatible no matter how wide you go.

### Signal flow

```
L,R → M/S decode → side → [LPF@crossover] → low side (≤100% width) ┐
                                          → high side (× width) ────┤→ M/S encode → [Soft Clipper] → Output
```

## 🎛️ Mini Reverb

Compact freeverb-style room simulator: two banks of four damped comb filters with detuned delay lengths (one bank per stereo channel) fed from a mono mix.

- **Room Size** (0–100%): feedback amount — short slap to long hall-like decay.
- **Damping** (0–100%): one-pole low-pass in each feedback loop — how quickly the tail loses treble.
- **Wet Mix** (0–100%): wet/dry blend.

Delay buffers are allocated from the core pool only while the effect is enabled (same pattern as surround), so no RAM is used when it's off.

## 📱 Usage

On device: **Settings → Sound Settings → Bassboost / Crystalizer / Air Exciter / Stereo Widener / Reverb**

### Bassboost menu
- **Enable**
- **Crossover** (40–500 Hz, step 10)
- **Sub Bass Gain** (0–24 dB, step 0.5) — gain added to the sub-bass frequencies.
- **Harmonics** (0–100%, step 5) — psychoacoustic upper harmonics mix to enhance perceived bass on small speakers.
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

### 🎯 Recommended settings for sub-bass on small drivers

```
Bassboost:
  Enable: ON
  Crossover: 80 Hz
  Sub Bass Gain: +12 dB
  Harmonics: 40%
  Output Gain: 0 dB
```

### 🎯 Recommended starting points for the new effects

```
Air Exciter:      Enable ON, Cutoff 3500 Hz, Intensity 30%
Stereo Widener:   Enable ON, Width 120%, Crossover 150 Hz
Reverb:           Enable ON, Room Size 50%, Damping 50%, Wet Mix 30%
```

## 📂 Files changed vs upstream

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
apps/plugin.h/.c                    — Plugin API: backlight_set_on_button_hold (API v285)
apps/plugins/lrcplayer.c            — Backlight Always On honors HOLD
lib/rbcodec/SOURCES                 — bassboost.c + crystalizer.c + exciter.c + widener.c + reverb.c
lib/rbcodec/dsp/dsp_proc_database.h — BASSBOOST + CRYSTALIZER + EXCITER + WIDENER + REVERB registered
lib/rbcodec/dsp/dsp_proc_settings.h — Includes all effect headers
lib/rbcodec/dsp/bassboost.c/.h      — Sub-bass isolator + Psychoacoustic Harmonics + Peak Limiter (wet branch)
lib/rbcodec/dsp/crystalizer.c/.h    — 2-band transient enhancer with mix
lib/rbcodec/dsp/exciter.c/.h        — Bandlimited treble even-harmonic generator
lib/rbcodec/dsp/widener.c/.h        — Mid/side stereo widener with mono bass
lib/rbcodec/dsp/reverb.c/.h         — freeverb-style damped comb reverb
```

## 🔧 Build

```bash
export PATH="/tmp/arm-bins:$PATH"
mkdir build-ipod6g && cd build-ipod6g
../tools/configure
# select: 29 (iPod Classic), N (normal build)
make -j$(nproc)
```

Copy `rockbox.ipod` to `/.rockbox/` on the iPod. Also copy `build-ipod6g/apps/lang/english.lng` to `/.rockbox/langs/`.

**Note:** After structural changes, delete `/.rockbox/config.cfg` or reset settings to avoid "Incompatible Version" errors.

## 🧮 ARM fixed-point notes

All DSP math is **fixed-point integer** (S7.24 / S15.16 / Q31) targeting ARM926EJ-S. Biquads, gain tables, envelope followers, and saturation use `FRACMUL` / `fp_factor` / `fp_sincos`.

- **Bassboost Peak Limiter**: envelope-based gain scaler — instant attack, one-pole exponential release (~100 ms), channels linked. `env >= |sample|` always holds, so the output can never exceed the threshold (`7/8 · 2^frac_bits`); because the limiting is linear gain, it adds no harmonics on sustained bass. This replaces an earlier memoryless waveshaper (`soft_over = headroom - (headroom * headroom) / (headroom + over)`) which flattened every cycle of an already-heavy bass waveform and sounded like clipping. **The limiter now lives on the wet branch**, not the full mix, so bass peaks no longer duck mids/highs. The other gain-adding stages (crystalizer, exciter, widener, reverb) still use the soft clipper, with threshold and ceiling tracking the format's real full scale (`2^frac_bits`, i.e. `2^27` for 16-bit sources).
- **Psychoacoustic Harmonics**: full-wave rectification `abs(x)` followed by a 1-pole DC blocker to generate even-order upper harmonics.
- **Crystalizer 2-band**: LR2 series — LP@60 → LP@3000 = band 0, remainder = band 1
- **Reverb**: freeverb comb tunings scaled by output rate; feedback set by Room Size, one-pole damped loop; delay buffers `core_alloc`'d only while enabled.

## 🎁 QoL features

### 📀 Go to Album (WPS context menu)

New item in the WPS context menu (Select button on iPod while playing):

- **Go to Album** — jumps straight to the current track's album in the Database browser
- Uses tagcache: locates the `"same"` menu (`%menu_start "same"` in `tagnavi.config`), finds its `Album` entry and opens it with the playing track's album preselected
- Falls back to the track's folder in the file browser when the Database isn't available (tagcache not ready or track has no album tag)

Implementation: `apps/onplay.c` — `go_to_album()` + `go_to_album_item`; `apps/tagtree.c` — `tagtree_goto_album()`; wired through `apps/root_menu.c`, `apps/tree.c` and `apps/gui/wps.c`.

### 🌗 Time-based Auto Brightness

Scheduled day/night backlight levels under **Settings → Display Settings → Timed Brightness** (targets with adjustable backlight brightness and an RTC, e.g. iPod 6G):

- **Enable** (default off) — turning it off restores the manual brightness setting
- **Day Time** (default 07:00) / **Night Time** (default 23:00) — set via the shared time picker screen, same UI as the alarm
- **Day Brightness** (default max, 63 on iPod 6G) / **Night Brightness** (default min, 1)

The controller applies whichever slot is currently active (the one whose trigger time most recently passed) and arms a single self-rearming timeout for the next transition — no per-minute polling, and the callback is ISR-safe. If the RTC has no valid time yet it falls back to manual brightness and retries when settings are applied.

Implementation: `apps/timed_brightness.c/.h`, menu items in `apps/menus/display_menu.c`, settings in `apps/settings_list.c`.

### 🎤 Lrcplayer: Backlight Always On honors HOLD

When you're in `Lrcplayer` showing lyrics and **Backlight Always On** is enabled (`Lrcplayer → Menu → Theme Settings → Backlight Always On`), the screen **stays on** even if you flip the HOLD switch. 🎉

- Reuses the existing setting — no new option.
- Only applies while Lrcplayer is active; on exit, the global HOLD/backlight behavior goes back to normal.
- Internally forces `backlight_on_button_hold = 2` (always on under hold) on entry and restores the original value on exit.

Implementation: `apps/plugins/lrcplayer.c` (`lrc_main`), new API `backlight_set_on_button_hold` in `apps/plugin.h`/`apps/plugin.c` (PLUGIN_API_VERSION 285).

## 🎧 Inline Earphone Remote (iPod 6G)

Play/pause and volume buttons on Apple inline earphones (headphone jack remote) are decoded via the "Mikey" I2C controller (bus 0, address 0x72):

- **Center button** — play/pause/resume on every screen (reported as a multimedia key, like the OF)
- **Volume +/−** — volume up/down, also works in menus and the file browser
- Keeps working with the hold switch on, like the OF

**Requirements:** a 6G unit with the "Mikey" chip (late-2008/2009 120/160GB models; the early-2007 80/160GB models lack it — check `Settings → Debug → View HW Info`, line `mikey remote ctrl` must show `ok` with headphones plugged in), and Apple-protocol earphones for the volume buttons (center play/pause may also trigger on remotes that short the mic line).

Credit: [Hemant Kumar](https://github.com/hemant6488) — [hemant6488/rockbox, ipod6g-mikey-v1](https://github.com/hemant6488/rockbox/releases/tag/ipod6g-mikey-v1); the patch was merged into official Rockbox upstream (commit `b217a55059`) and is included in this fork.
