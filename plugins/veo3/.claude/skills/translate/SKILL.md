---
name: veo3-translate
description: >-
  Localize the Veo3 plugin UI. Audit every string that is placed into a UI
  control (Button, Label, ComboBox, window title, status text), route each
  user-facing string through CTranslate::...->Translate(L"..."), and make sure
  translation.json has an entry for that key in all 14 supported languages.
  Use when the user asks to localize/translate the Veo3 plugin, add a language,
  fix an untranslated UI string, or extend translation.json.
---

# Veo3 — UI translation skill

## Goal

Make the entire Veo3 plugin UI fully localizable and localized:

1. **Audit.** Find every place in the C++ source where text is written into a UI
   element (`Button`, `Label`, `ComboBox`, window caption, status line, etc.).
2. **Route.** Make sure each *user-facing* string is supplied through
   `CTranslate::GetInstance().GetManager()->Translate(L"...")` (in-process UI
   code) — not a hardcoded literal. The English source string is itself the
   lookup key.
3. **Fill.** For every translation key, ensure `translation.json` has an entry
   in **all 14 languages**. Add any missing translations.

Do **not** translate proper nouns (see the exclusion list). Do **not** make any
change that is not part of routing or translating UI text. Do **not** touch
version control (no `git add`/`commit`/`branch`/`push`/`stash` — leave the
working tree for the user to review and commit).

## Supported languages (all 14 must be present for every key)

```
en-US  cs-CZ  de-DE  es-ES  fr-FR  ru-RU  zh-CN
pt-BR  sr-Cyrl-RS  sr-Latn-RS  ja-JP  sq-AL  it-IT  ar-SA
```

`en-US` is the source/identity map: its value equals the key. There is **no
inheritance** — a key missing from any block silently renders the English
source for that locale, with no compile-time warning. Every key goes into every
block.

## How translation works in this project (recap)

- `translation.json` (UTF-8, **no BOM**) sits next to `Veo3.rc` and is embedded
  as the `IDR_TRANSLATION` `RCDATA` resource. It is **compiled into the DLL** —
  editing it has no effect until the project is rebuilt.
- Lookup key = the English source string, e.g. `Translate(L"Save")`.
- In-process UI code uses
  `CTranslate::GetInstance().GetManager()->Translate(L"...")`, which returns a
  `std::wstring`. The common local alias is
  `CTranslateManager* tr = CTranslate::GetInstance().GetManager();` then
  `tr->Translate(L"...")`.
- A missing key is **silent** — it returns the English source. There is no
  compile-time check, so the audit must be done by reading the code.

## Procedure

### Step 1 — Inventory every UI string

Search the source (`src/`) for the calls that put text on screen. The text
argument of each of these is a candidate string:

- `AVS::CreateButton(..., <text>, ...)`
- `AVS::CreateLabel(..., <text>, ...)`
- `AVS::CreateComboBox(..., { <item>, <item>, ... })`  — combo box items
- `AVS::Label_SetText(<hwnd>, <text>)`
- `AVS::Label_SetTextAndColor(<hwnd>, <text>, <color>)`
- `AVS::Button_SetSettings(<hwnd>, <settings>, <text>)`
- `SetWindowText(<hwnd>, <text>)`
- `CreateWindowEx(..., <window title>, ...)` — window captions
- `AVS::CreateTextEditMultiline(...)` placeholder/initial text, if any
- String literals stored in `#define` macros that feed any of the above
  (e.g. `FIRST_IMAGE`, `LAST_FRAME`, `PREV_VIDEO`), and literal arrays such as
  `vToggleGroupNames`.

For each candidate, classify it:

- **Translatable** — user-facing words/phrases → must go through `Translate`
  and exist in `translation.json` for all 14 languages.
- **Proper noun / not translatable** → leave as a literal (see exclusion list).

Grep starting points (ripgrep):

```
CreateButton|CreateLabel|CreateComboBox|Label_SetText|Label_SetTextAndColor|Button_SetSettings|SetWindowText|CreateWindowEx
```

### Step 2 — Route translatable strings through `Translate`

For any translatable string currently passed as a raw literal, replace the
literal with `tr->Translate(L"<English source>").c_str()` (or
`CTranslate::GetInstance().GetManager()->Translate(L"<English source>").c_str()`
where no local `tr` alias exists). Keep the English source text identical to the
JSON key — they must match exactly (whitespace, punctuation, case).

Notes specific to this codebase:

- Mode-toggle buttons are set in **two** places: at `WM_CREATE` and again in the
  `Button_SetSettings` calls inside the `WM_COMMAND` toggle handlers, and a
  third time via the `vToggleGroupNames` array used to restore the previously
  active button's caption. All three must use the same translated key, or a
  button's caption will revert to English when toggled.
- Comparison logic relies on some label strings (e.g.
  `if (file1 != FIRST_IMAGE)`, `!= PREV_VIDEO`, `!= FIRST_FRAME`). If you
  localize a label whose text is also compared against the stored macro, keep
  the comparison consistent — compare against the same translated value that was
  set, not a different literal. Prefer keeping the macro as the single source of
  the translated string (assign `tr->Translate(...)` into the macro's place /
  a variable used both to set and to compare) so set-text and compare-text can
  never diverge. Do **not** change the generation/branching behavior — only the
  displayed text and the value it is compared to.

### Step 3 — Reconcile `translation.json`

For every translatable key:

- If the key is missing entirely, add it to **all 14** language blocks.
- If the key exists in some blocks but not others, fill the gaps.
- Fix key mismatches between code and JSON. Known existing mismatch:
  the code calls `Translate(L"Duration (s)")` but the JSON only defines
  `"Duration"` — so the label is always English. Resolve by making the key in
  the code and the key in JSON identical (add a `"Duration (s)"` entry to every
  language, or change the code to `L"Duration"` — pick one and apply it
  consistently). Prefer adding `"Duration (s)"` so the visible label keeps the
  unit hint.

Keep the JSON valid, UTF-8 **without BOM**, and preserve the existing 4-space
indentation and key ordering style. Use correct diacritics / native script for
each language (e.g. `Größe`, `Durée`, `期間`, `الحجم`, `Дуже`/Cyrillic where
applicable) — never ASCII-fold accented characters.

### Step 4 — Report

Summarize: which strings were already translated, which were newly routed
through `Translate`, which keys were added to `translation.json`, and which
strings were intentionally left untranslated (with the reason — proper noun,
technical/debug-only, etc.). Remind the user that **a rebuild is required**
because `translation.json` is an embedded `RCDATA` resource.

## Do NOT translate (proper nouns & non-text tokens)

Leave these as literals — they are brand/model names, codes, or non-linguistic
tokens:

- Application / brand: `Veo3`, `Veo 3.1`, `Veo 3.1 Fast`, `Veo 3.1 Lite`.
- Model codes: `veo-3.1-generate-preview`, `veo-3.1-fast-generate-preview`,
  `veo-3.1-lite-generate-preview`.
- Aspect ratios: `16:9`, `9:16`.
- Resolutions: `720p`, `1080p`, `4k`.
- Numeric duration items: `4`, `6`, `8`.
- File-dialog filter strings and file extensions (`*.mp4`, `*.png`, …) — these
  are OS filter syntax, not UI labels (localize only the human-readable filter
  description part if the user explicitly asks, but by default leave them).
- API / structured-protocol tokens, JSON field names, `[SUCCESS]`/`[ERROR]`/
  `[WARNING]` markers, log text, and `OutputDebugStringA` strings.

## Candidate strings found in the current code (audit baseline)

This is the inventory as of the last review of `src/veo3_ui.cpp`. Re-verify
against the live source before editing — line numbers drift.

**Already routed through `Translate` (verify each key exists in all 14 langs):**

- `Save`, `Cancel`, `Delete API-key` — settings window buttons.
- `Settings` — settings window title and the main-window Settings button.
- `Model`, `Orientation`, `Size` — combo-box labels.
- `Generate` — generate button (also toggled to `Cancel` while running).
- `Veo3` — main window title → **proper noun, do not translate** (key may exist
  but its value should stay `Veo3` in every locale).

**Translatable but currently hardcoded (need routing + JSON entries):**

- Mode toggle buttons / `vToggleGroupNames`: `Text to Video`, `Image to Video`,
  `First + Last Frame`, `Extend Video`.
- File buttons: `Add File` (×3).
- File-path placeholder labels: `Add file #1`, `Add file #2`, `Add file #3`.
- Macros: `First Image`, `Second Image`, `Third Image`, `First Frame`,
  `Last Frame`, `Video from a previous generation`.
- Status / error messages shown via `Label_SetTextAndColor` (decide with the
  user whether these count as UI text to localize; several are
  cache/diagnostic): `Could not find cache for this file.`,
  `The video is outdated`, `Unable to open file cache.json`,
  `aspectRatio and resolution must match the original video`. Treat purely
  internal exception/`what()` text and `[cache.json] …`/debug strings as
  non-UI unless the user says otherwise.

**Known key mismatch to fix:** `Duration (s)` (code) vs `Duration` (JSON).

## Hard constraints

- Only routing UI text through `Translate` and editing `translation.json` are in
  scope. Do not refactor unrelated code, change layout/behavior, or alter
  generation logic.
- Keep the English source string and the JSON key byte-for-byte identical.
- A new key must be added to **every** one of the 14 language blocks.
- `translation.json` must stay valid UTF-8 **without BOM**.
- After editing the JSON, a **rebuild** is required for the change to take
  effect (embedded resource).
- **Never** perform any version-control action. Leave all changes uncommitted
  for the user to review.
