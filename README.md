# 🎸 Rockbox Bassboost + Crystalizer

Bass booster, Crystalizer, Air Exciter, Stereo Widener y Mini Reverb DSP stages para Rockbox — pensado para iPod Classic 6G/7G. 🎧

> Sub-bass que se siente, no que se ve.armónicos psychoacústicos para que el grave se escuche incluso en drivers chicos.

## 🔊 Bassboost

Un procesador de sub-bass simple pero potente, con armónicos psychoacústicos (estilo MaxxBass) para hacer audible el grave en auriculares/drivers pequeños.

- **Crossover** (40–500 Hz): Linkwitz-Riley 4º orden (-24 dB/oct) para aislar el sub-bass.
- **Sub Bass Gain** (0–24 dB, step 0.5, default +12 dB): ganancia fija sobre la banda de sub-bass.
- **Harmonics** (0–100%): generador de armónicos psychoacústicos (rectificación de onda completa + bloqueo DC). Crea armónicos pares que engañan al cerebro para que perciba graves profundos.
- **Output gain** (±12 dB): trim maestro de la rama procesada.
- **Peak Limiter**: limiter de pico linked-channel sobre la **rama wet** (ataque instantáneo, release exponencial ~100 ms). Cuando los picos boosteados superan el techo (~ −1.9 dBFS), se escala la rama de graves linealmente en vez de aplanar la onda. Al estar en la rama wet, **no duckea medios/agudos** — el bombeo molesto del limiter global se elimina. Los canales comparten un gain, así que la imagen estéreo no se corre.

### Defaults

| Parámetro | Default |
|-----------|---------|
| Crossover | 80 Hz |
| Sub Bass Gain | +12 dB |
| Harmonics | 0% |
| Output gain | 0 dB |

### Signal flow

```
                 ┌→ [Sub Bass Gain] → [Harmonics + DC Block] → [Branch Gain] → [Peak Limiter (wet only)] → ┐
Input → LR4 LPF ┤                                                                                          ├→ (+) → Output
                 └─────────────────────── dry (mids/highs sin tocar) ─────────────────────────────────────┘
```

## ✨ Crystalizer

Enhancer de transientes a 2 bandas:

- **Crossover LR2 12 dB/oct** a 60 Hz y 3000 Hz
- **Detección de pico por 2ª derivada** por banda: `d²[n] = x[n] − 2·x[n-1] + x[n-2]`
- **Enhancement**: `output = band + intensity × d²` (sin pre-ringing)
- **Intensity Mid** (−24 a +24 dB) para 60–3000 Hz
- **Intensity High** (−24 a +24 dB) para 3000 Hz+
- **Mix** (0–100%) wet/dry
- **Output gain** (±12 dB)
- **Granularidad 0.1 dB** en todos los controles

### Signal flow

```
Input → [LPF@60] → [LPF@3000] → Band Mid (60-3000) → enhancer → ┐
                 → [HPF@3000] → Band High (3000+) → enhancer → ├→ Mix → Out
```

## 🌬️ Air Exciter

Generador de armónicos pares bandlimited para agudos — añade "aire" y detalle sin aspereza. Solo la banda de agudos aislada se procesa non-linear, así los armónicos se quedan en el treble en vez de intermodular todo el espectro.

- **Cutoff** (2000–8000 Hz): LR4 high-pass (-24 dB/oct) aislando la banda a excitar.
- **Intensity** (0–100%): cuánto se mezcla de vuelta.

Usa los mismos bloques MaxxBass que el Harmonics del bassboost (rectify + DC block 1-pole) sobre la señal high-passed.

### Signal flow

```
Input → LR4 HPF@cutoff → [Rectify + DC Block] → [× Intensity] → ┐
                                                                  ├→ [Soft Clipper] → Output
Input ----------------------------------------------------------┘
```

## 🎚️ Stereo Widener

Control de width mid/side con graves controlados.

- **Width** (0–200%): escala la señal side. 100% transparente, 0% mono, >100% ensancha.
- **Crossover** (50–500 Hz): el side por debajo de esta freq nunca supera unity width → graves mono-compatibles por más que abras.

### Signal flow

```
L,R → M/S decode → side → [LPF@crossover] → low side (≤100% width) ┐
                                          → high side (× width) ────┤→ M/S encode → [Soft Clipper] → Output
```

## 🎛️ Mini Reverb

Reverb tipo freeverb compacto: dos bancos de 4 comb filters con damping (un banco por canal estéreo) alimentados desde un mix mono.

- **Room Size** (0–100%): feedback — de slap corto a hall largo.
- **Damping** (0–100%): low-pass 1-pole en cada loop — qué tan rápido pierde agudos la cola.
- **Wet Mix** (0–100%): wet/dry.

Los buffers de delay se alocan del core pool solo mientras el efecto está activo (igual que surround), así que no gastan RAM apagado.

## 📱 Usage

En el dispositivo: **Settings → Sound Settings → Bassboost / Crystalizer / Air Exciter / Stereo Widener / Reverb**

### Bassboost menu
- **Enable**
- **Crossover** (40–500 Hz, step 10)
- **Sub Bass Gain** (0–24 dB, step 0.5) — ganancia añadida al sub-bass.
- **Harmonics** (0–100%, step 5) — mix de armónicos superiores para perceived bass.
- **Output Gain** (±12 dB, step 0.5)

### Crystalizer menu
- **Enable**
- **Intensity Mid** (−24 a +24 dB, step 0.1)
- **Intensity High** (−24 a +24 dB, step 0.1)
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

### 🎯 Recommended settings para sub-bass en drivers chicos

```
Bassboost:
  Enable: ON
  Crossover: 80 Hz
  Sub Bass Gain: +12 dB
  Harmonics: 40%
  Output Gain: 0 dB
```

### 🎯 Puntos de partida para los efectos nuevos

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
apps/plugins/lrcplayer.c            — Backlight Always On respeta HOLD
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

Copiar `rockbox.ipod` a `/.rockbox/` en el iPod. También copiar `build-ipod6g/apps/lang/english.lng` a `/.rockbox/langs/`.

**Nota:** Después de cambios estructurales, borrá `/.rockbox/config.cfg` o reseteá settings para evitar "Incompatible Version".

## 🧮 ARM fixed-point notes

Toda la matemática DSP es **fixed-point integer** (S7.24 / S15.16 / Q31) para ARM926EJ-S. Biquads, gain tables, envelope followers y saturación usan `FRACMUL` / `fp_factor` / `fp_sincos`.

- **Bassboost Peak Limiter**: gain scaler basado en envelope — ataque instantáneo, release exponencial 1-pole (~100 ms), canales linked. `env >= |sample|` siempre, así la salida nunca supera el threshold (`7/8 · 2^frac_bits`). Como el limiting es gain lineal, no añade armónicos en graves sostenidos. Reemplaza un waveshaper memoryless anterior (`soft_over = headroom - (headroom * headroom) / (headroom + over)`) que aplanaba cada ciclo de un bass ya pesado y sonaba a clipping. **Ahora el limiter vive en la rama wet**, no en la mezcla completa, así que los picos de graves no duckean medios/agudos. Los demás stages que añaden gain (crystalizer, exciter, widener, reverb) siguen usando el soft clipper, con threshold y ceiling trackeando el full scale real del formato (`2^frac_bits`, i.e. `2^27` para 16-bit).
- **Psychoacoustic Harmonics**: rectificación full-wave `abs(x)` + DC blocker 1-pole para generar armónicos pares superiores.
- **Crystalizer 2-band**: serie LR2 — LP@60 → LP@3000 = banda 0, resto = banda 1
- **Reverb**: tunings de comb freeverb escalados por output rate; feedback seteado por Room Size, loop damped 1-pole; buffers `core_alloc`'d solo cuando está habilitado.

## 🎁 QoL features

### 📀 Go to Album (WPS context menu)

Item nuevo en el context menu del WPS (botón Select en iPod mientras reproducís):

- **Go to Album** — salta directo al álbum del track actual en el Database browser
- Usa tagcache: localiza el menu `"same"` (`%menu_start "same"` en `tagnavi.config`), busca su entrada `Album` y la abre con el álbum del track preseleccionado
- Fallback a la carpeta del track en el file browser cuando el Database no está disponible (tagcache no listo o track sin tag de álbum)

Implementación: `apps/onplay.c` — `go_to_album()` + `go_to_album_item`; `apps/tagtree.c` — `tagtree_goto_album()`; cableado a través de `apps/root_menu.c`, `apps/tree.c` y `apps/gui/wps.c`.

### 🌗 Time-based Auto Brightness

Brillo day/night programado bajo **Settings → Display Settings → Timed Brightness** (targets con brillo ajustable y RTC, ej. iPod 6G):

- **Enable** (default off) — al apagarlo restaura el brillo manual
- **Day Time** (default 07:00) / **Night Time** (default 23:00) — seteados via time picker compartido, misma UI que la alarma
- **Day Brightness** (default max, 63 en iPod 6G) / **Night Brightness** (default min, 1)

El controller aplica el slot activo (el cuya trigger time pasó más recientemente) y arma un único timeout self-rearming para la próxima transición — sin polling por minuto, y el callback es ISR-safe. Si el RTC no tiene hora válida todavía, cae a brillo manual y reintenta cuando se aplican settings.

Implementación: `apps/timed_brightness.c/.h`, menu items en `apps/menus/display_menu.c`, settings en `apps/settings_list.c`.

### 🎤 Lrcplayer: Backlight Always On respeta HOLD

Cuando estás en `Lrcplayer` mostrando lyrics y activás **Backlight Always On** (`Lrcplayer → Menu → Theme Settings → Backlight Always On`), la pantalla **no se apaga** aunque pongas el switch de HOLD. 🎉

- Reutiliza la setting existente — no hay opción nueva.
- Solo aplica mientras Lrcplayer está activo; al salir, el comportamiento global de HOLD/backlight vuelve a la normalidad.
- Internamente fuerza `backlight_on_button_hold = 2` (always on under hold) al entrar y restaura el valor original al salir.

Implementación: `apps/plugins/lrcplayer.c` (`lrc_main`), API nueva `backlight_set_on_button_hold` en `apps/plugin.h`/`apps/plugin.c` (PLUGIN_API_VERSION 285).

## 🎧 Inline Earphone Remote (iPod 6G)

Play/pause y volumen en los Apple earphones inline (remote del jack) se decodifican via el controller I2C "Mikey" (bus 0, address 0x72):

- **Center button** — play/pause/resume en cualquier pantalla (reportado como multimedia key, como el OF)
- **Volume +/−** — volumen up/down, también en menús y file browser
- Sigue funcionando con hold switch on, como el OF

**Requisitos:** un 6G con chip "Mikey" (modelos late-2008/2009 120/160GB; los early-2007 80/160GB no lo tienen — checkeá `Settings → Debug → View HW Info`, la línea `mikey remote ctrl` debe decir `ok` con auriculares plugueados), y earphones Apple-protocol para los botones de volumen (center play/pause puede dispararse también en remotes que cortocircuitan la línea de mic).

Crédito: [Hemant Kumar](https://github.com/hemant6488) — [hemant6488/rockbox, ipod6g-mikey-v1](https://github.com/hemant6488/rockbox/releases/tag/ipod6g-mikey-v1); el patch fue mergeado a Rockbox upstream (commit `b217a55059`) y está incluido en este fork.
