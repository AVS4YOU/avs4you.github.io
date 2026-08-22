---
description: Adjust an existing effect plugin's look (stronger, faster, denser, different colour) and rebuild it
argument-hint: <slug> <what to change, e.g. "effect-vhs stronger noise and more tracking error">
allowed-tools: Read, Write, Edit, Bash, Glob, Grep, SendUserFile
---

Adjust the look of an existing effect plugin: **$ARGUMENTS**

1. Read the plugin's `dllmain.cpp`. The tuning constants live in the anonymous
   namespace at the top - the particle budget, the layer table (share, length,
   radius, speed, alpha) and the grading amounts.
2. Before editing, you can explore the effect's own positional parameters
   without rebuilding: `ApplyEffect` reads `intensity, speed/wind, density, ...`
   from `(paramCount, params)`, and `check_effect.py` shows how to pass BSTRs
   through ctypes. Use that to find the value you want, then bake it in.
3. Make the change, rebuild and re-render:
   `python tools/plugin-tools/build_effect.py <slug>`
4. Re-verify: `python tools/plugin-tools/check_effect.py <slug>`. Coherence and
   determinism must still pass, and watch the ms/frame - raising density raises
   cost linearly.
5. Extract a few frames from the new GIF and look at them before declaring it
   done. Send the GIF to the user.

Do not change the animation model while tuning: positions stay closed-form in
`CClock` time, identity stays hash-derived. See
[docs/EffectPlugin-Recipes.md](docs/EffectPlugin-Recipes.md). Do not commit
unless asked.
