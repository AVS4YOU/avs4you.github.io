---
description: Build, package, preview and verify an existing plugin, then refresh index.html
argument-hint: <plugin folder name, e.g. effect-vhs> [extra flags for build_effect.py]
allowed-tools: Read, Bash, Glob, Grep, SendUserFile
---

Build and verify the plugin **$ARGUMENTS**.

For an effect plugin (`plugins/<slug>/config.json` has `"type": "effect"`):

```bash
python tools/plugin-tools/build_effect.py $ARGUMENTS
python tools/plugin-tools/check_effect.py $ARGUMENTS
```

The first builds Release Win32 + x64, zips each DLL into
`build/<arch>/<slug>.avsp`, renders the preview GIF and runs `package.py`. The
second loads the DLL and checks exports, determinism, temporal coherence and
ms/frame.

For a content plugin, build its `.sln` for `Release|Win32` and `Release|x64`
with the `v142` toolset, re-zip the `.avsp` with the DLL plus any bundled
runtime files it already contains (check the existing archive first so nothing
is dropped), and run `python package.py`.

If anything fails, diagnose and fix it rather than reporting the raw error.
Report the artefact paths and, for effects, the coherence and ms/frame numbers.
Send the preview GIF if it was regenerated. Do not commit unless asked.
