---
name: avs-content-plugin
description: Build or modify an AVS4YOU content plugin - the kind that adds a menu item and its own Win32 dialog to a host app and returns a media file (AI generators like Veo3/Sora2/HeyGen/NanoBanana/SunoApi, downloaders like the YouTube plugin, TTS like Supertonic3/StableAudio3). Use for requests about a plugins/ folder whose config.json has "type": "content", about GetPluginMenu / ClickMenuItem / AsyncCallback, translation.json, module.manifest, .avsp packaging of a content plugin, or adding a dialog/API integration to a host app.
---

# AVS4YOU content plugin

A content plugin is a Windows DLL that adds menu entries to a host app, opens
its own Win32 UI when clicked, does work (call an API, run a tool, generate
media) and hands a resulting file path back through an async callback.

`docs/ContentPlugin-README.md` is the ABI reference. `plugins/veo3/CLAUDE.md` is
a detailed architecture write-up of a mature example - read it before designing
a new one.

## Shape of the ABI

Unlike effect plugins, content plugins are **instanced**: the host calls
`CreatePlugin()` and passes that handle to every other call, so per-instance
state belongs on your plugin object, never in statics.

Required exports (see `plugins/nano-banana/module.def` for the canonical list):

```
CreatePlugin DeletePlugin PluginType PluginId PluginName PluginVersion
PluginIcon IsApplicationSupported ReleasePluginString SetLanguage
GetMenuForContext GetPluginMenu GetIconById ClickMenuItem SetCallbackHandler
SetParentWindow SetTemporaryPath CleanTemporaryFiles CleanCachedData PluginInfo
```

`PluginType()` returns `Plugins::PluginType::Content`.

Flow:

1. `GetMenuForContext(handle, ContextType)` returns a JSON array of
   `{ "text", "icon", "action" }` for the context the host is asking about
   (`MediaLibrary`, `Video`, `Audio`, `Text`, `Image`).
2. The host calls `ClickMenuItem(handle, id)` - open a modal window here and
   pump messages.
3. When the result file is ready, call the callback registered through
   `SetCallbackHandler` with `(PluginId(), path, 0, context)`. The path string
   must be allocated with `export_str` - the host frees it via
   `ReleasePluginString`.

## Conventions this repo follows

- **Sources in `src/`**, project files at the plugin root. `plugin.{h,cpp}` is
  the object, `exports.cpp` the ABI surface, `*_ui.cpp` the Win32 dialogs,
  `export_utils.cpp` holds `export_str` and `TR`.
- **Resources are compiled in**, not shipped loose. The `.rc` embeds
  `translation.json` and `icon.ico` as `RCDATA` and `module.manifest` as
  `RT_MANIFEST`; the plugin extracts what it needs to
  `%LOCALAPPDATA%\avs_plugin_<name>\` on first run. Editing `translation.json`
  requires a rebuild.
- **i18n**: `CTranslate::GetInstance().Init(g_hInst, IDR_TRANSLATION)` in
  `DllMain`, `SetLanguage` forwards a BCP-47 tag, and lookups use the English
  source string as the key. A missing key silently falls back to English, and a
  new key must be added to *every* language block. 14 languages ship today - see
  the Translations section of `plugins/veo3/CLAUDE.md`.
- **UI**: use the themed widgets in `sdk/ui/winapi/ui.h` (`AVS::CreateButton`,
  `AVS::CreateTextEditMultiline`, `AVS::Color::GetDefaultWindowBackground`)
  rather than raw `CreateWindow`, so plugins look consistent.
- **Long-running work runs off the UI thread** and reports progress by posting
  messages back to the window. `plugins/youtube/src/external_process_with_childs.h`
  (`NSProcesses::CProcessManager`) is the shared pattern for driving a child
  process with piped stdout/stderr, and it kills the whole process tree via a
  job object.
- **API keys** live unencrypted in `%LOCALAPPDATA%\avs_plugin_<name>\app.key`.
  Set `requiresKey: true` in `config.json` when the plugin needs one.
- **Bundled tools go inside the `.avsp`.** The YouTube plugin ships `yt-dlp.exe`
  and `ffmpeg.exe` next to its DLL in the ZIP, and locates them relative to the
  loaded module directory.

## Build and publish

Content plugins do not have a one-shot script - they vary too much (some use
CMake, some MSBuild, some vendor static libs). Build the `.sln`/`.vcxproj` for
`Release|Win32` and `Release|x64` with the `v142` toolset, then package:

```bash
# .avsp is a plain ZIP: the DLL plus anything it needs at runtime
python - <<'PY'
import zipfile
with zipfile.ZipFile("plugins/<slug>/build/x86/<Name>.avsp", "w", zipfile.ZIP_DEFLATED) as z:
    z.write("plugins/<slug>/Release/<Name>.dll", "<Name>.dll")
PY

python package.py     # refresh index.html and plugins.json from every plugins/*/config.json
```

To ship a new build, bump `version` in `config.json`, run `python release.py <folder>`
to publish the packages, then `python package.py`, and push only after the release
exists. For sora2 the rebuild also overwrites the force-tracked
`plugins/sora2/build/x86/Sora2.avsp`, which Pages serves to installers from before
`plugins.json` (see CLAUDE.md, Build facts) - commit it together with the release.

`config.json` needs `slug`, `pluginId`, `version`, `"type": "content"`,
`"typeLabel": "Content Plugin"`, and `media` pointing at the icon (content
plugins use `icon.ico`, not a GIF). `pluginId` is exactly the string
`PluginId()` returns: installers read it from `plugins.json` to find the
installed plugin, `package.py` refuses a config without it, and `release.py`
refuses a package whose DLL does not contain it. There is no scaffolder for
content plugins, so add it by hand - see `docs/PluginsManifest.md`.

Debug builds of several plugins set `OutDir` to
`%APPDATA%\AVS4YOU\Plugins\<Name>.plugin` so the host loads them directly -
close the host app before rebuilding or the DLL is locked.

## Shared SDK

Treat `sdk/include/` as read-only - it is the ABI for every plugin.

- `sdk/common/` - `utils.cpp` (`NSStringUtils::replace`, UTF-8 conversions),
  `CHttpClient`, `CIconExtractor`
- `sdk/translate/` - `CTranslate` singleton, JSON backed
- `sdk/ui/winapi/ui.h` - themed Win32 widgets
- `sdk/3dParty/` - curl, `nlohmann/json` (single header), vcpkg-style static libs

## Examples to copy from

| Plugin | Pattern |
|---|---|
| `veo3` | drives a generated PowerShell script per request, has a full CLAUDE.md |
| `sora2`, `nano-banana`, `heygen` | HTTP API + key settings dialog |
| `youtube` | bundles and drives an external binary, progress from piped stdout |
| `Supertonic3`, `StableAudio3` | local ONNX inference, extra DLLs in the `.avsp` |
