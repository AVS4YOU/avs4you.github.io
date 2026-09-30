---
name: avs-effect-plugin
description: Build a complete AVS4YOU video/image effect plugin from a one-line description ("rain on video", "snow", "old film", "heat haze", "glitch"), or modify/rebuild/preview an existing effect-* plugin. Covers the whole pipeline - scaffold, write the C++ ApplyEffect, build x86+x64, package the .avsp, render the marketplace preview GIF, verify temporal coherence and speed, and register it in index.html. Use whenever the request is about an AVS4YOU image/video effect, a plugins/effect-* folder, sdk/effects/effect_common.h, or the PreviewGenerator.
---

# AVS4YOU effect plugin

Turn "write a plugin that puts rain on video" into a built, verified,
marketplace-registered effect in one pass.

An effect plugin is a Windows DLL exporting a fixed C ABI. The host hands it a
BGRA frame plus a time value; the plugin mutates the pixels in place. The
deliverable is not the `.cpp` - it is a `build/x86/<slug>.avsp` package, a
preview GIF, a `config.json`, and an updated `index.html`.

## Read this first

Load [docs/EffectPlugin-Recipes.md](../../../docs/EffectPlugin-Recipes.md)
before writing any rendering code. It is short and it contains the things that
decide whether the result looks professional:

- the time argument is ambiguous - use `NSEffect::CClock`
- animation must be a **closed-form function of time**, never `pos += step`,
  and particle properties must come from a **hash of the particle index**, never
  `rand()`
- what scales with resolution and what must not (a real bug shipped here)
- how to make an overlay visible over bright footage
- the performance budget and what actually costs time

`docs/EffectPlugin-README.md` is the ABI reference. The scaffold written by
`new_effect.py` already has the exports, the state handling and the clock
right; the shipped effect plugins under `plugins/effect-*` show the variants.

## Default interpretation of a one-line request

Unless the user says otherwise, assume they want the *good* version:

- **animated and continuous** across frames - a weather/particle effect that
  re-randomises per frame is a bug, not a style
- **depth**: three or four layers with different size, speed and opacity, plus
  a slightly out-of-focus foreground layer
- **the scene reacts**, not just an overlay pasted on top (rain implies a cool,
  desaturated, hazy, vignetted picture)
- **constant strength** over the clip - do not ramp intensity with
  `dCompleteness` unless asked; that argument is timeline position, not a dial
- resolution independent, and fast enough to scrub
- supports Video Converter, Video Editor and Image Converter

Do not stop to ask which of these they want. Build it, then say what you chose
and what is easy to change.

## Pipeline

### 1. Scaffold

```bash
python tools/plugin-tools/new_effect.py effect-snow \
  --effect-name Snow \
  --desc "Drifting snowfall with parallax depth layers and wind." \
  --apps "Video Converter,Video Editor,Image Converter" \
  --tint "linear-gradient(135deg,#7FB3FF,#E9F3FF)"
```

Slug convention is `effect-<thing>`. This writes `dllmain.cpp`,
`Effect.vcxproj`, `Effect.vcxproj.filters`, `<name>.sln`, `module.def` and
`config.json`, already wired to the SDK and already using the coherent particle
pattern. Pick a `--tint` gradient that matches the effect's mood - it is the
marketplace card background.

### 2. Write the effect

Only two functions in the generated `dllmain.cpp` normally need work:

- `Draw(...)` - the particles or distortion
- an atmosphere/grade pass, if the effect should change the whole picture

Leave the exports, the state handling and the clock alone; they are correct.

Structure worth keeping for a particle effect:

```cpp
struct Layer { float share, lengthNorm, radiusRef, speed, alphaHead, alphaTail; };
const Layer LAYERS[] = { /* far, mid, near, foreground bokeh */ };
```

Then per particle: hash the index for identity, derive phase from `time`, draw.

`sdk/effects/effect_common.h` provides `CClock`, `HashU32`/`Rand01`/`RandRange`,
`Noise1D`/`Noise1D2` (smooth, safe for time), `NoisePixel` (grain),
`CSurface::SplatBlend`/`SplatAdd`/`StreakBlend`/`SampleBilinear`, `ParamFloat`,
and `ExportString`. Prefer these over hand-rolled equivalents - the splat
falloff and the clock are both load-bearing.

Effects that resample the frame (twist, ripple, displacement) need a copy of the
source before writing - see `plugins/effect-spiral/dllmain.cpp`. Effects that
only add on top do not.

### 3. Build, package, preview

```bash
python tools/plugin-tools/build_effect.py effect-snow
```

Builds Release Win32 + x64, zips each DLL into `build/<arch>/<slug>.avsp`,
renders the preview GIF through `tools/PreviewGenerator`, and runs `package.py`
to refresh `index.html`. Useful flags: `--arch x86`, `--no-preview`,
`--no-package`, `--duration`, `--fps`, `--side-by-side`, `--ping-pong`,
`--extra FILE...` (models or extra DLLs to put inside the `.avsp`).

Iterate with `--arch x86 --no-preview --no-package` while the code is in flux;
do a full run at the end.

### 4. Verify - do not skip this

```bash
python tools/plugin-tools/check_effect.py effect-snow
```

Loads the DLL in-process and checks exports, `PluginType`, the info strings,
`PluginId` and `IsApplicationSupported` against `config.json`, determinism,
motion direction, **temporal coherence**, and ms per 1080p frame.

Read the output, do not just check the exit code:

| Symptom | Cause |
|---|---|
| determinism FAIL | `rand()` / `srand(time(...))` somewhere - use `HashU32` |
| "barely correlate" | positions are re-randomised per frame |
| "no more alike than distant" | nothing is carried between frames |
| slow warning | see the performance section of the recipes doc |
| `IsApplicationSupported` disagrees | `config.json` apps and the `switch` differ |
| `PluginId()` vs config.json pluginId | the literal in `PluginId()` was edited - keep the two identical |

Then **look at the GIF**. Extract a few frames and view them - metrics do not
catch "it reads as scratches on the lens". A contact sheet of frames 0, 12, 25
and 40 side by side is usually enough.

### 5. Report

Say what was built, where the `.avsp` landed, the coherence and ms/frame
numbers, the design choices made, and which constants are the tuning knobs.

## Notes that save time

- **VS 2019 / `v142`.** A Build Tools install without the C++ workload has
  `MSBuild.exe` but no `Microsoft.Cpp.props` and fails with MSB4019 -
  `build_effect.py` already selects an install that has the workload.
- **Never edit `sdk/include/`.** It is the ABI, shared by every plugin.
  `sdk/effects/effect_common.h` is additive and safe to extend - but changes
  there affect every effect plugin, so keep them backward compatible.
- **New export = two edits**: `module.def` and the `extern "C"` block. On Win32
  an export missing from the `.def` is invisible to the host.
- `plugins/*/Release/`, `x64/`, `Debug/` and `build/` are gitignored - packages
  are published as GitHub releases by `release.py`. The preview GIF is
  committed. Exception: two legacy packages are force-tracked for old
  installers, effect-vhs x86 and Sora2 x86 (see CLAUDE.md, Build facts), so
  rebuilding effect-vhs changes `plugins/effect-vhs/build/x86/effect-vhs.avsp`.
- `config.json` drives the marketplace card. `package.py` inlines every
  `plugins/*/config.json` into `index.html` and `plugins.json`, so run it (or
  let `build_effect.py` run it) after touching one. Required: `slug`,
  `pluginId` and `version`. `pluginId` must be exactly what `PluginId()`
  returns - installers use it to find the installed plugin (see
  `docs/PluginsManifest.md`); `new_effect.py` writes both from one value.
- Do not commit unless the user asks.

## Existing plugins worth reading

| Plugin | Why |
|---|---|
| `effect-vhs` | per-frame state via `effectData`, multi-pass full-frame processing |
| `effect-spiral` | resampling/distortion with a source copy, minimal plugin |
| `effect-restoration-gaterv3` | shipping an ONNX model and extra DLLs inside the `.avsp` |
