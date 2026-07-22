# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

`Veo3.vcxproj` builds **`Veo3.plugin.dll`**, a Windows Content Plugin (`Plugins::PluginType::Content`) for AVS4YOU host apps (Video Editor / Video Converter — see `IsApplicationSupported` in `src/exports.cpp`). The DLL wraps Google's Veo 3 video-generation API by driving a generated PowerShell script per request.

This is one of several sibling plugins under `../` (heygen, sora2, effect-*); they all share the SDK at `../../sdk/`.

## Build

Visual Studio 2019 / `v142` toolset, C++ Default, Unicode, `/MTd` (Debug Win32). Open `Veo3.sln`, build `Debug|Win32` or `Release|x64`.

- **Debug|Win32 deploys directly to the host:** `OutDir` is `$(APPDATA)\AVS4YOU\Plugins\Veo3.plugin`. The host app loads from there at startup — closing the app before rebuilding is required when the DLL is locked.
- The exported symbol set is fixed by `module.def`; adding a new export means editing both `module.def` and `src/exports.cpp`.
- `Veo3.rc` embeds `translation.json` and `icon.ico` as `RCDATA` resources, plus `module.manifest` as `RT_MANIFEST`. Resources are extracted to `%LOCALAPPDATA%\avs_plugin_veo3\` at first run (see `CVeo3Plugin` ctor in `src/plugin.cpp`).
- There are no tests and no lint step in this project.

## High-level architecture

```
Host app  ──(stdcall C exports from module.def)──►  exports.cpp
                                                       │
                                                       ▼
                                              CVeo3Plugin (plugin.cpp)
                                              ├── m_engine: CVeo3        (request engine)
                                              ├── m_workDirectory        (%LOCALAPPDATA%\avs_plugin_veo3)
                                              └── m_hWindow / m_hParentWindow
                                                       │
                          UI thread (veo3_ui.cpp)  ◄───┤───► engine thread (veo3.cpp)
                          Win32 modal dialogs           │     builds script.ps1 from
                          - main prompt window          │     scripts_bodys.h templates
                          - settings (API key)          │     │
                                                        │     ▼
                                                        │   powershell -ExecutionPolicy Bypass
                                                        │     │
                                                        │     ▼
                                                        │   NSProcesses::CProcessManager
                                                        │   (veo3_utils.cpp) — pipes
                                                        │   stdout/stderr, forwards to
                                                        │   CVeo3Plugin::ProcessCallback
                                                        │     │
                                                        ▼     ▼
                                              PostMessage(WmMainWindowCommands::Output / OutputStop)
                                              back to the main window proc.
```

Key boundaries to keep in mind:

- **`dllmain.cpp`** initializes `CTranslate` from the `IDR_TRANSLATION` resource on `DLL_PROCESS_ATTACH`, and unregisters `Veo3MainWindowClass` / `Veo3SettingsWindowClass` on `DLL_PROCESS_DETACH`. Window classes are registered lazily by `ShowPromptWindow` / `ShowSettingsWindow`.
- **`exports.cpp`** is the only thing the host calls. Every returned `wchar_t*` is allocated via `export_str` and must be freed by the host through `ReleasePluginString`. Adding state to the plugin means adding it on `CVeo3Plugin`, not as a static.
- **`CVeo3::Process`** picks a template (`SCRIPT_TEXT_TO_VIDEO` / `SCRIPT_IMAGE_TO_VIDEO` / `SCRIPT_FIRST_AND_LAST_FRAME` / `SCRIPT_EXTEND_VIDEO` from `scripts_bodys.h`), `NSStringUtils::replace`s `${PARAM_…}` placeholders, writes `script.ps1` + `script.prompt` into a fresh temp work dir, then starts PowerShell. The prompt text is passed via the sidecar file (not the command line) to avoid quoting issues. Adding a new generation mode = new template + new `case` here + a new `WmMainWindowCommands::Button…` in `veo3_ui.h`.
- **PowerShell scripts emit `[SUCCESS]` / `[ERROR]` / `[WARNING]` lines via `Write-Cmd`**; the C++ side treats every stdout line as a structured update. Logs (separate from the stdout channel) go to `.Veo3.log` inside the work dir.
- **API key** is stored unencrypted at `%LOCALAPPDATA%\avs_plugin_veo3\app.key` (UTF-8, no BOM) by the settings window and substituted into scripts as `${PARAM_KEY}`. Anything that touches the key path lives in `veo3_ui.cpp` (settings dialog) and `veo3.cpp` (substitution).
- **i18n:** see the "Translations" section below — non-trivial enough to be its own topic.
- **Per-instance work dir vs. per-request work dir:** `CVeo3Plugin::m_workDirectory` is persistent (`%LOCALAPPDATA%\avs_plugin_veo3`, holds icon + `app.key` + `cache.json`). `CVeo3::CreateWorkDirectory` makes a fresh `%TEMP%\AVS*` directory per generation, which is where `script.ps1`, `script.prompt`, the `.mp4` output, and `.Veo3.log` live.

## Shared SDK (`../../sdk/`)

Treat as read-only from this plugin's perspective unless you're explicitly working on the SDK — changes ripple to every sibling plugin.

- `include/` — plugin ABI (`CContentPluginIntf.h`, `CEffectPluginIntf.h`, `CBase.h`, `AVSConsts.h`). The set of supported host app IDs lives in `AVSConsts.h`.
- `common/` — `utils.cpp` (`NSStringUtils::replace`, UTF-8 ↔ wstring), `CHttpClient`, `CIconExtractor`.
- `translate/` — `CTranslate` singleton + `CTranslateManager`, JSON-backed.
- `ui/winapi/ui.h` — themed Win32 widgets used throughout (`AVS::CreateButton`, `AVS::CreateTextEditMultiline`, `AVS::Color::GetDefaultWindowBackground`, etc.). Prefer these over raw `CreateWindow` to keep visuals consistent across plugins.
- `3dParty/` — `curl`, `nlohmann/json` (single-header), vcpkg-style static libs. Include path used directly: `../../../sdk/3dparty/nlohmann/json/single_include/nlohmann/json.hpp` (lowercase `3dparty` works on Windows because the FS is case-insensitive, but the on-disk folder is `3dParty`).

Plugin contract reference: `docs/ContentPlugin-README.md` (root of the repo). The Content vs Effect distinction is enforced by `PluginType()`.

## Translations

### How it works

1. **Storage.** `translation.json` (UTF-8) lives next to `Veo3.rc`, which embeds it as a resource: `IDR_TRANSLATION RCDATA "translation.json"`. The file is *not* shipped separately — it's compiled into the DLL, so any edit requires a rebuild.
2. **Load.** `DllMain` calls `CTranslate::GetInstance().Init(g_hInst, IDR_TRANSLATION)` on `DLL_PROCESS_ATTACH` (`src/dllmain.cpp:18`). `CTranslateManager` (`../../sdk/translate/`) is a process-wide singleton, copies the raw resource bytes into `m_content`, but does **not** parse the JSON yet.
3. **Language selection.** The host calls `SetLanguage(handle, L"<bcp47>")` (`src/exports.cpp:67`), which forwards to `CTranslateManager::SetLang`. That call parses the JSON, looks up the top-level key matching the requested language tag, and stores the inner object in `m_obj`. If the tag isn't found, `m_obj` stays empty and every subsequent lookup falls back to the original key (effectively English).
4. **Lookup.** Two equivalent entry points:
   - `TR(L"Save")` — defined in `src/export_utils.cpp`. Returns a freshly allocated `wchar_t*` (caller frees via `ReleasePluginString`). Use this when handing a string back to the host.
   - `CTranslate::GetInstance().GetManager()->Translate(L"Save")` — returns `std::wstring`. Use this for in-process UI code (every call site in `veo3_ui.cpp` does this; the local `tr` alias is just `tr = CTranslate::GetInstance().GetManager()`).
5. **Key = English source string.** The lookup key is itself the English text (`L"Save"`, `L"Delete API-key"`, etc.). The `en-US` block in `translation.json` is therefore an identity map kept around for completeness — `SetLang("en-US")` and "no language set" both render identically.

### Available languages (14)

`translation.json` ships these top-level keys (BCP-47 tags with region):

| Tag           | Language                |
|---------------|-------------------------|
| `en-US`       | English (US) — source   |
| `cs-CZ`       | Czech                   |
| `de-DE`       | German                  |
| `es-ES`       | Spanish (Spain)         |
| `fr-FR`       | French                  |
| `it-IT`       | Italian                 |
| `pt-BR`       | Portuguese (Brazil)     |
| `ru-RU`       | Russian                 |
| `sq-AL`       | Albanian                |
| `sr-Cyrl-RS`  | Serbian (Cyrillic)      |
| `sr-Latn-RS`  | Serbian (Latin)         |
| `ar-SA`       | Arabic (Saudi Arabia)   |
| `zh-CN`       | Chinese (Simplified)    |
| `ja-JP`       | Japanese                |

The host is expected to pass these exact tags to `SetLanguage` — `"en"` or `"de"` alone will miss and fall back to English.

### Gotchas to watch for when editing strings

- **No compile-time check that a `Translate(L"X")` key exists.** A missing key silently returns the English source. Two real instances of this in the current code:
  - `src/veo3_ui.cpp:408` calls `Translate(L"Duration (s)")`, but `translation.json` only defines `"Duration"` — that label is always English in every locale.
  - The mode-toggle labels (`L"Text to Video"`, `L"Image to Video"`, `L"First + Last Frame"`, `L"Extend Video"` in `src/veo3_ui.cpp:282-287`) and the `FIRST_IMAGE` / `SECOND_IMAGE` / `THIRD_IMAGE` / `FIRST_FRAME` / `LAST_FRAME` / `PREV_VIDEO` macros are passed through directly without going through `Translate(...)` at all. If you localize them, both the call site and `translation.json` need the new key.
- **A new key must be added to *every* language block**, or those locales will silently show English for that string. There's no merge/inheritance — the lookup is one flat dictionary per language.
- **Rebuild required after editing `translation.json`** — it's a `RCDATA` resource, not a runtime file. Touching the JSON without rebuilding has no effect on the deployed DLL.
- **Encoding:** UTF-8 (no BOM). The resource bytes are read raw and parsed by `nlohmann::json`, which expects UTF-8.
