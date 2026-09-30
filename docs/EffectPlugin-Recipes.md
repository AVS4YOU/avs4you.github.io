# Effect Plugin Recipes

`EffectPlugin-README.md` documents the ABI. This document covers the part that
decides whether an effect looks professional or amateur: **how to animate**.

The numbers quoted below were measured while building an animated rain effect
with this pipeline. That plugin was dropped for looking wrong, but the failures
it walked into are the general ones, which is why they are written down here.

---

## 1. The time argument is not what the header says

The ABI declares:

```cpp
HRESULT ApplyEffect(BYTE* data, int width, int height, double timestamp,
                    int paramCount, const BSTR* params, void** effectData);
```

The 4th argument is documented as `timestamp` in seconds, but every shipped
plugin names it `dCompleteness` and the hosts in the field pass a **0..1
progress ratio through the effect**. `tests/effect/test.cpp` and
`tools/PreviewGenerator` both drive it as a ratio.

Never depend on which one it is. `NSEffect::CClock` in
[`sdk/effects/effect_common.h`](../sdk/effects/effect_common.h) removes the
ambiguity: it measures the average step between two consecutive calls and
rebuilds a frame index from it, so

```
CClock::Time() == frameIndex / 25.0      // seconds
```

whether the host counts in ratios or in seconds, and regardless of how long the
clip is. It also detects seeks and scrubs, and keeps advancing when the host
pins the value (which `PreviewGenerator -force-completeness` does).

```cpp
state->clock.Tick(dCompleteness);
const float time = (float)state->clock.Time();
```

---

## 2. Frame-to-frame coherence: closed form, not accumulation

The single most common way to get this wrong:

```cpp
// WRONG - a fresh random layout every frame. Reads as flicker, not motion.
for (int i = 0; i < count; ++i)
{
    int x = rand() % w;
    int y = rand() % h;
    drawDrop(x, y);
}
```

Two things fix it, and both are needed.

### 2.1 Give every particle a permanent identity

Derive a particle's properties from a **hash of its index**, never from a random
number generator. The hash returns the same value on every frame, on every
machine, in any order:

```cpp
const unsigned int seed = HashU32(0x9e3779b9U + (unsigned int)i * 0x9e3779b9U);

const float lane   = Rand01(seed);                          // horizontal slot
const float phase0 = Rand01(seed ^ 0xa1b2c3d4U);            // where in its cycle it starts
const float speed  = RandRange(seed ^ 0x1f83d9abU, 0.8f, 1.25f);
```

`rand()`, `srand(time(...))`, `Math.random`-style state, or anything seeded per
frame all break this. `check_effect.py` fails the determinism check when they
are used.

### 2.2 Make position a function of time, not a running total

```cpp
// GOOD - closed form. Position depends only on `time`.
const float cycle = travel / (speed * fh);      // seconds for one full pass
const float phase = Fract(phase0 + time / cycle);
const float y     = phase * travel - length;
```

Why not `y += speed * dt`? An accumulator drifts, and it is wrong whenever the
host does not feed frames in order: seeking, scrubbing, re-rendering a segment,
or splitting a render across threads all hand you a time that is not
`previous + 1`. A closed form is correct for any time, in any order, always.

Keep `**effectData` for the clock and for genuinely path-dependent state, not
for positions.

### 2.3 Move the whole field together

Global motion - wind, gusts, camera shake, flicker - should come from **one
smooth function of time shared by every particle**, so the field moves as one
body instead of dissolving:

```cpp
const float tilt = wind + 0.18f * Noise1D2(time * 0.13f, 7771U);
```

`Noise1D`/`Noise1D2` are smooth and continuous in their argument, so they are
safe to feed time into. `NoisePixel` is not - it is per-pixel static, for grain.

---

## 3. Scale rules: what to scale by height, what not to scale at all

This is subtle and it is easy to ship a bug here. The rule that works:

| Quantity | Scale by |
|---|---|
| lengths, widths, radii, displacements | frame **height** (`h / 720`) |
| particle **count** | **nothing** (only aspect ratio) |
| per-pixel grade strength | nothing |

Sizes scaling with height means a 1080p frame is a *magnified* 720p frame. A
magnified picture contains the same number of objects - so scaling the count by
frame area as well multiplies the real density. Getting this wrong is easy to
miss: the 320x180 preview looks right while 1080p renders a solid wall.
Correct version:

```cpp
const float sizeScale = fh / REF_H;
const float aspect    = Clampf((fw / fh) / (REF_W / REF_H), 0.4f, 3.0f);
const float budget    = PARTICLES_REFERENCE * aspect * density * intensity;
```

Also clamp small radii. At 320x180 a "1.3 px at 720p" streak becomes 0.33 px and
disappears into rounding; a floor of ~0.5 px keeps thin layers visible in
previews:

```cpp
const float radius = max(0.5f, layer.radiusRef * sizeScale);
```

---

## 4. Making an overlay read on any footage

A bright overlay vanishes over bright content. Water, glass and dust all
*refract*, which means they show as a **dark edge around a bright core**. Draw
two passes:

```cpp
// wide, soft, dark - keeps the drop visible over bright areas
s.StreakBlend(x0, y0, x1, y1, radius * 1.85f, SHADE_B, SHADE_G, SHADE_R,
              aHead * 0.28f, aTail * 0.28f, 1.7f /* coarse sampling is fine */);

// narrow, bright core on top
s.StreakBlend(x0, y0, x1, y1, radius, RAIN_B, RAIN_G, RAIN_R, aHead, aTail);
```

Depth also matters more than detail. Three or four layers with different size,
speed, opacity and one deliberately out-of-focus foreground layer read as volume;
one layer reads as scratches on the lens.

And an overlay alone rarely convinces. A weather effect needs the *scene* to
change too - light desaturation, a cool cast, lifted blacks, a soft vignette.
Run that grade **before** drawing the particles, so they sit on top of the
graded picture.

---

## 5. Performance

An effect runs on every frame of an export and on every timeline scrub. Budget
roughly **under 40 ms per 1920x1080 frame**; `check_effect.py` warns past 120 ms.

What actually moved the needle on one effect, 161 ms -> 39 ms per 1080p frame:

1. **Fix the density rule first** (section 3). It was 4x of the cost and it was
   also a visual bug.
2. **No `sqrt` in splat falloff.** A `1 - (d/r)^2` falloff needs only the
   squared distance. `CSurface::SplatBlend` does it this way.
3. **Coarser sampling for wide soft passes.** `StreakBlend`'s `stepScale`
   parameter: a low-alpha blur halo at `1.7` is indistinguishable from `1.0` and
   costs 40% less.
4. **Collapse per-pixel chains into a per-row matrix.** Desaturation + colour
   cast + haze is one affine map; derive its coefficients once per row. Hoist
   `dx*dx` for a vignette into a per-row lookup.
5. **Reject invisible particles early**, before any drawing.

Measure, do not guess - `check_effect.py` reports ms/frame, and positional
parameters let you time individual passes without rebuilding.

---

## 6. Parameters

`GetEffectParams` is meant to return a JSON schema (see
`EffectPlugin-README.md`), but **no shipped plugin does** - they all return
`NULL`, and hosts call `ApplyEffect` with `paramCount = 0, params = NULL`.

Match that: return `NULL`, and read parameters defensively so the plugin is
ready if a host starts supplying them.

```cpp
params.intensity = Clampf(ParamFloat(nParamCount, sParams, 0, 1.0f), 0.0f, 2.0f);
params.wind      = Clampf(ParamFloat(nParamCount, sParams, 1, 0.20f), -0.75f, 0.75f);
```

Document the positional order in a comment. It is also the fastest way to
A/B tune an effect from `check_effect.py` without rebuilding.

---

## 7. Things the host expects

- **BGRA, 4 bytes per pixel**, rows top to bottom, `width * height * 4` bytes.
  Channel 0 is blue. Getting this backwards makes every colour effect look wrong.
- **Modify `data` in place.** Do not reallocate it.
- **Leave alpha alone** unless the effect is genuinely about transparency.
- **Never let an exception cross the DLL boundary.** Return an `HRESULT`.
- **Validate inputs** and return `E_INVALIDARG` for a null buffer or a
  non-positive size.
- **Release what you allocate**: every `wchar_t*` through `ReleasePluginString`,
  every `effectData` through `ReleaseEffectData`. A host that never keeps state
  should still get a sensible frame - fall back to a clock on the stack.
- **`module.def` is the contract on Win32.** `__stdcall` names are decorated
  there, so an export missing from the `.def` is invisible to the host even
  though `__declspec(dllexport)` is present. Add new exports in both places.

---

## 8. Build, preview, verify, publish

```bash
python tools/plugin-tools/new_effect.py effect-snow --effect-name Snow --desc "..."
python tools/plugin-tools/build_effect.py effect-snow
python tools/plugin-tools/check_effect.py effect-snow
```

- `new_effect.py` scaffolds the folder from `tools/plugin-tools/templates/`,
  already wired to the SDK and already using the coherent particle pattern.
- `build_effect.py` builds Release Win32 + x64, zips each DLL into
  `build/<arch>/<slug>.avsp` (an `.avsp` is a plain ZIP), renders the preview
  GIF through `tools/PreviewGenerator`, and runs `package.py` to refresh
  `index.html`.
- `check_effect.py` loads the DLL in-process and checks exports, determinism,
  motion direction, temporal coherence and ms/frame. Coherence is the one to
  watch: neighbouring frames must correlate several times better than distant
  ones.

`PreviewGenerator` renders 320x180 previews, which is the size the marketplace
cards use. Note that it now swizzles R/B around the plugin call - `stb_image`
and `gif.h` are RGBA while the plugin ABI is BGRA - so preview colours match the
host.

---

*Scaffold: `tools/plugin-tools/templates/dllmain.cpp.tmpl`*
*Shared helpers: `sdk/effects/effect_common.h`*
