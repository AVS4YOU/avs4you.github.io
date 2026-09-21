# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this repository is

The **AVS4YOU plugin marketplace**: a static GitHub Pages site
(<https://avs4you.github.io>) plus the source of every plugin it serves.

`index.html` is the whole storefront - a single self-contained page. The plugin
catalogue is *generated*: `package.py` reads every `plugins/*/config.json` and
inlines them as a JSON blob into `<script type="application/json"
id="plugins-data">`. The same records also go to `plugins.json` at the site
root, so anything resolving a download reads that instead of scraping the
page. **Never hand-edit either** - edit the `config.json` and run
`python package.py`.

## Layout

```
index.html                 storefront; plugin data is generated into it
plugins.json               generated machine-readable catalogue + release URLs
package.py                 config.json x N  ->  index.html + plugins.json
release.py                 build/<arch>/*.avsp  ->  GitHub release assets
docs/                      plugin developer documentation
sdk/                       shared code for all plugins (see below)
plugins/<slug>/            one folder per plugin, each with config.json
tools/PreviewGenerator/    plugin DLL + PNG -> animated preview GIF
tools/plugin-tools/        scaffold / build / verify scripts for effect plugins
tests/effect/              minimal host that loads an effect DLL and applies it
tests/content/             same for content plugins
```

### `sdk/`

- `include/` - **the plugin ABI**. `CEffectPluginIntf.h`,
  `CContentPluginIntf.h`, `CBase.h`, `AVSConsts.h` (host app ids). Treat as
  read-only; a change here ripples to every plugin.
- `effects/effect_common.h` - header-only helpers for effect plugins:
  `CClock` (temporal coherence), hash-based random, `CSurface` (BGRA access,
  anti-aliased splats and streaks), positional parameter reading.
- `common/` - `utils.cpp` (string conversions), `CHttpClient`, `CIconExtractor`
- `translate/` - `CTranslate`, JSON-backed i18n
- `ui/winapi/ui.h` - themed Win32 widgets, so plugin dialogs look consistent
- `3dParty/` - curl, `nlohmann/json`, vcpkg-style static libs

### Plugin types

`config.json` `type` is either `effect` or `content`.

- **effect** - a DLL exporting `ApplyEffect`; the host hands over a BGRA frame
  and a time value and the plugin mutates pixels in place. No UI, no instance.
- **content** - a DLL that adds menu items and its own Win32 dialogs to a host
  app and returns a produced media file through an async callback. Instanced via
  `CreatePlugin`/`DeletePlugin`.

## Build facts

- **Visual Studio 2019, `v142` toolset, Unicode, `/MT`** (`/MTd` for Debug).
- A `.avsp` package is **a plain ZIP** containing the plugin DLL plus whatever it
  needs at runtime (extra DLLs, ONNX models, bundled `.exe` tools).
- `module.def` is the export contract. On Win32, `__stdcall` names are decorated,
  so an export missing from the `.def` is invisible to the host even with
  `__declspec(dllexport)`. **A new export means editing both** `module.def` and
  the `extern "C"` block.
- `plugins/*/Release/`, `x64/`, `Debug/` and `build/` are gitignored: packages
  are served from GitHub releases (`release.py`). Preview media are committed.
  Exception: installers shipped before `plugins.json` support hardcode
  `plugins/effect-vhs/build/x86/effect-vhs.avsp` and
  `plugins/sora2/build/x86/Sora2.avsp`, so exactly those two are force-tracked
  (`git add -f`) and served by Pages. Refresh them when effect-vhs or sora2
  gets a new version; drop them once those installers are out of circulation.
- A Build Tools install without the C++ workload has `MSBuild.exe` but no
  `Microsoft.Cpp.props` and fails with MSB4019. `tools/plugin-tools` picks an
  install that has the workload.

## `plugins.json` is a public contract

Besides `index.html`, the manifest is read by AVS4YOU installers: at install
time they download it and parse it with a hand-written Pascal JSON reader to
offer sample plugins. Shipped installers are never updated, so treat the file
like an API. Format and full rules:
[docs/PluginsManifest.md](docs/PluginsManifest.md).

- Installers need `schema`, and per plugin `slug`, `pluginId`,
  `downloads.x86` and `downloads.x64`. `pluginId` is the string the DLL's
  `PluginId()` returns - the plugin's install folder name - and has to match
  it exactly: `check_effect.py` compares the two, and `release.py` refuses a
  package whose DLLs do not contain it.
- ASCII only. `package.py` writes the file with `ensure_ascii=True` and stops,
  naming the `config.json`, on a missing or malformed slug/pluginId/version
  (`release.py:check_identity`) or a slug/pluginId repeated ignoring case.
- `MANIFEST_SCHEMA` (`package.py`): adding a field never bumps it. Renaming,
  removing or retyping `slug`, `pluginId` or `downloads.x86/x64` does - and
  every installer in the field then stops offering plugins.
- Publish order: run `release.py` **before** pushing a version bump. Pages
  serves the new URLs at once, and until the release exists they are a 404.
- Never remove or rename a slug that a shipped default list names: today
  `effect-vhs` and `veo3` (build_tools `defaults`, `<module>-plugins`).
- Installers download the packages over https with urlmon, which follows
  GitHub's redirect to the signed asset URL (isxdl cannot: a ~930-character
  redirect target corrupts its memory). They only accept release URLs under
  `https://github.com/AVS4YOU/` - keep `RELEASE_REPO` there.
- The two force-tracked legacy packages (see Build facts) serve installers from
  before the manifest. Leave them in place while those are in circulation.

## Working on effect plugins

Read [docs/EffectPlugin-Recipes.md](docs/EffectPlugin-Recipes.md) before writing
rendering code, and use the `avs-effect-plugin` skill for the full pipeline. The
non-obvious parts:

- `ApplyEffect`'s 4th argument is declared `timestamp` but hosts pass a 0..1
  progress ratio. Use `NSEffect::CClock`, which works either way.
- Animation must be a **closed-form function of time** and particle identity
  must come from a **hash of the index** - not `rand()`, not `pos += step`.
  Otherwise the effect flickers instead of moving, and it breaks on seeking.
- Sizes scale with frame height; particle **counts must not scale with area** or
  density multiplies on large frames.

```bash
python tools/plugin-tools/new_effect.py effect-snow --effect-name Snow --desc "..."
python tools/plugin-tools/build_effect.py effect-snow   # build x86+x64, .avsp, GIF, index.html
python tools/plugin-tools/check_effect.py effect-snow   # exports, determinism, coherence, ms/frame
```

The scaffold in `tools/plugin-tools/templates/dllmain.cpp.tmpl` already
implements this pattern; `plugins/effect-vhs/` shows per-frame state through
`effectData`, `plugins/effect-spiral/` a minimal resampling effect.

## Working on content plugins

Use the `avs-content-plugin` skill. `plugins/veo3/CLAUDE.md` is a detailed
architecture write-up of a mature example, including how the i18n resource
pipeline works. Sources go in `src/`, resources are compiled into the DLL via
the `.rc`, and editing `translation.json` requires a rebuild.

## Conventions

- Existing sources are CRLF, mostly UTF-8 with BOM. Match the file you edit.
- Do not commit or push unless asked.
- Do not add a plugin to `index.html` by hand; add `config.json` and run
  `package.py`.
