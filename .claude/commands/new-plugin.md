---
description: Build a new AVS4YOU plugin from a one-line description, picking the right plugin type automatically
argument-hint: <description, e.g. "rain on video" or "generate music from a text prompt">
allowed-tools: Read, Write, Edit, Bash, Glob, Grep, Skill, SendUserFile
---

Build a new AVS4YOU plugin for: **$ARGUMENTS**

First decide which of the two plugin types this is, then follow that skill end
to end without stopping to ask.

**Effect plugin** (`avs-effect-plugin` skill) - it transforms the picture frame
by frame: weather, particles, distortion, colour grading, film looks, glitches,
blurs, upscaling/restoration. Signal: the request describes something happening
*to the video or image itself*.

**Content plugin** (`avs-content-plugin` skill) - it adds a menu item and a
dialog to the host app and produces a new media file: AI generation, text to
speech, downloading, uploading, anything that calls a service or drives an
external tool. Signal: the request describes *creating or fetching* media, or
mentions an API, a key, a prompt or a URL.

If it is genuinely both, prefer the effect plugin and say so.

Content plugins have no scaffolder: write their `config.json` by hand, and
include `pluginId` - exactly the string the DLL's `PluginId()` returns, e.g.
`Sora2.plugin`. `package.py` refuses a config without it. For effect plugins
`new_effect.py` fills it in.

State which type you picked and why in one line, then build it: scaffold,
implement, build x86 + x64, package the `.avsp`, render the preview, verify, and
run `package.py` so it appears in `index.html`. Report the result with the paths
and the numbers. Do not commit unless asked.
