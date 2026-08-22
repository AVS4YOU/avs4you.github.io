---
description: Build a complete AVS4YOU effect plugin from a one-line description (e.g. /new-effect rain on video)
argument-hint: <description of the effect, e.g. "rain on video" or "falling snow, heavy, windy">
allowed-tools: Read, Write, Edit, Bash, Glob, Grep, Skill, SendUserFile
---

Build a complete, working AVS4YOU effect plugin for: **$ARGUMENTS**

Invoke the `avs-effect-plugin` skill and follow it end to end. Do not ask
clarifying questions first - make the good default choices the skill describes,
build the thing, and report what you chose afterwards.

Deliver all of it in one pass:

1. Scaffold with `tools/plugin-tools/new_effect.py` (slug `effect-<thing>`).
2. Implement the effect. Read
   [docs/EffectPlugin-Recipes.md](docs/EffectPlugin-Recipes.md) first - the
   animation must be temporally coherent: closed-form in `CClock` time, particle
   identity from `HashU32`, never `rand()` per frame, never `pos += step`.
   Include depth layers and an atmosphere pass unless that makes no sense for
   this effect.
3. Build and package both architectures and render the preview GIF:
   `python tools/plugin-tools/build_effect.py effect-<thing>`
4. Verify: `python tools/plugin-tools/check_effect.py effect-<thing>`.
   Every check must pass, coherence included, and the frame time should be
   under ~40 ms at 1080p.
5. Look at the GIF yourself - extract a few frames into the scratchpad and view
   them. Fix what looks wrong, do not just trust the metrics.
6. Send the finished GIF to the user with `SendUserFile`.

Then report: what was built, the `.avsp` paths, the coherence and ms/frame
numbers, the visual design decisions, and which constants to tweak to change
intensity, speed and density. Do not commit unless asked.
