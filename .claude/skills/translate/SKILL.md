---
name: ui-translate
description: >-
  Localize the UI of a project specified at invocation time. Audit every
  string that is placed into a UI control (Button, Label, ComboBox, window
  title, status text) in the target project's source, route each user-facing
  string through the project's existing translation lookup mechanism, and
  make sure translation.json has an entry for that key in all 13 supported
  languages, in the fixed order en-US, ru-RU, de-DE, fr-FR, es-ES, it-IT,
  ja-JP, nl-NL, ko-KR, pt-BR, pl-PL, da-DK, zh-CN. Keeps translated strings
  short enough to fit the existing UI controls. Use when the user asks to
  localize/translate a project's UI, add a language, fix an untranslated UI
  string, or extend a project's translation.json.
---

# UI translation skill (any project)

## Goal

Given a **target project path** supplied by the user at invocation, make that
project's UI fully localizable and localized:

1. **Locate.** Identify the target project directory and its existing
   translation infrastructure (translation lookup function/macro,
   `translation.json` or equivalent resource file, how it's loaded/compiled).
2. **Audit.** Find every place in the project's source where text is written
   into a UI element (`Button`, `Label`, `ComboBox`, window title, status
   line, menu item, tooltip, etc.).
3. **Route.** Make sure each *user-facing* string is supplied through the
   project's translation lookup call — not a hardcoded literal. The English
   source string is itself the lookup key.
4. **Fill.** For every translation key, ensure `translation.json` has an
   entry in **all 13 languages**, in the fixed order defined below. Add any
   missing translations.
5. **Fit.** Keep every translated string short enough to fit the UI control
   it's displayed in (see "Length constraints" below).

Do **not** translate proper nouns (see the exclusion list — adapt it to the
target project's own brand/model/technical tokens). Do **not** make any
change that is not part of routing or translating UI text. Do **not** touch
version control (no `git add`/`commit`/`branch`/`push`/`stash` — leave the
working tree for the user to review and commit).

## Step 0 — Resolve the target project

Before doing anything else, determine which project to work on:

- If the user gave a path or project name when invoking the skill, use that.
- If it's ambiguous or missing, ask which project/directory to localize
  before scanning any code. Do not assume a specific project by default —
  this skill is not tied to any single codebase.

All subsequent steps operate strictly inside that project's directory tree.
Never touch files belonging to a different project in the same workspace.

## Supported languages (all 13 must be present for every key, in this order)

```
en-US  ru-RU  de-DE  fr-FR  es-ES  it-IT  ja-JP
nl-NL  ko-KR  pt-BR  pl-PL  da-DK  zh-CN
```

- `en-US` — English (source/identity map: its value equals the key)
- `ru-RU` — Russian
- `de-DE` — German
- `fr-FR` — French
- `es-ES` — Spanish
- `it-IT` — Italian
- `ja-JP` — Japanese
- `nl-NL` — Dutch
- `ko-KR` — Korean
- `pt-BR` — Portuguese (Brazil)
- `pl-PL` — Polish
- `da-DK` — Danish
- `zh-CN` — Chinese (Simplified)

Always emit language blocks/keys in **exactly this order** in
`translation.json`, regardless of the order they appear in in an existing
file — when editing an existing file, reorder it to match if it doesn't
already.

There is **no inheritance** between locales — a key missing from any block
silently falls back to showing the raw key or the English source (behavior
depends on the project's lookup implementation; verify which it is). Every
key must therefore go into every one of the 13 blocks explicitly.

## Step 1 — Discover how translation works in this project

Do not assume a specific API. Inspect the target project to find:

- The existing `translation.json` (or equivalently named resource) — its
  location, encoding (expect UTF-8, **no BOM** unless the project clearly
  uses one already), indentation style, and current key ordering.
- How that file is loaded at runtime (embedded resource compiled into a
  binary, loaded from disk at startup, bundled as an asset, etc.) — this
  determines whether a **rebuild** is required for changes to take effect,
  which you must mention in the final report.
- The translation lookup call used in UI code (function, method, or macro),
  e.g. patterns like `Translate(L"...")`, `tr->Translate(...)`,
  `i18n.t("...")`, `_("...")`, `gettext(...)`, `LocalizedString(...)`, etc.
  Grep the source for the call used near UI-construction code and note its
  exact signature and how the lookup key relates to the English string.
- Whether there's already a local alias/pattern (e.g. a `tr` shortcut) used
  consistently across the codebase — if so, keep using it for consistency
  rather than introducing a second style.

If no translation infrastructure exists yet, propose creating a minimal one
consistent with the project's language/framework, and confirm with the user
before adding a new dependency or a new file layout.

## Step 2 — Inventory every UI string

Search the project's source for the calls that put text on screen. Typical
candidates (adapt to whatever UI framework the target project actually uses
— native win32/AVS-style APIs, Qt, GTK, web/React, Electron, etc.):

- Button / label / combo-box / list-item constructors and setters
- Window titles and dialog captions
- Status bars, tooltips, placeholder text, menu items
- String literals stored in constants/macros that feed any of the above
  (e.g. arrays of toggle-group captions, mode names)

For each candidate, classify it:

- **Translatable** — user-facing words/phrases → must go through the
  project's translation call and exist in `translation.json` for all 13
  languages.
- **Proper noun / technical token / not translatable** → leave as a literal
  (see exclusion list).

Use a broad grep first, then read matches in context — the same string
literal may appear both in a UI-construction call (translatable) and in
comparison logic against a variable that must stay in sync (see below).

## Step 3 — Route translatable strings through the lookup call

For any translatable string currently passed as a raw literal, replace the
literal with a call to the project's translation lookup, following the exact
style discovered in Step 1. Keep the English source text identical to the
JSON key — they must match exactly (whitespace, punctuation, case).

Watch for these common pitfalls in any project:

- **Strings set in multiple places.** A toggle/mode caption is often set in
  more than one spot (initial creation, state-change handlers, and any
  "restore previous state" array/logic). All occurrences of the same logical
  string must route through the same translated key, or the caption will
  revert to English (or another language) inconsistently when the UI state
  changes.
- **Strings used in comparisons.** If a label's text is also compared
  against a stored constant elsewhere in the code (e.g. to detect which mode
  is active), keep the comparison consistent with what's actually displayed.
  Prefer making the translated value the single source of truth for both the
  displayed text and the compared value, so they can never diverge. Do
  **not** change branching/generation behavior — only the displayed text and
  the value it is compared to.

## Step 4 — Reconcile `translation.json`

For every translatable key:

- If the key is missing entirely, add it to **all 13** language blocks, in
  the fixed order above.
- If the key exists in some blocks but not others, fill the gaps.
- Fix key mismatches between code and JSON (e.g. code calls the lookup with
  one string but the JSON defines a slightly different one, so the label
  silently falls back). Make the code key and the JSON key byte-for-byte
  identical — prefer keeping whichever variant carries more information
  (e.g. a unit hint), and apply that choice consistently on both sides.

Keep the JSON valid, correctly encoded per Step 1's findings, and preserve
the existing indentation/formatting style. Use correct diacritics / native
script for each language (proper accented characters, native scripts for
ja-JP, ko-KR, zh-CN, etc.) — never ASCII-fold or romanize.

## Step 5 — Length constraints (fit the existing controls)

Translated strings must not break the layout of the UI element they render
in:

- Where the project defines explicit control dimensions (fixed button width,
  fixed-width label, character limits, etc.), keep each translation within
  that limit. Check nearby layout code/resource definitions for hints.
- Where no explicit limit is defined, use the **English source string's
  length as the practical budget**: aim to keep each translation within
  roughly the same length as the source, and flag any translation that is
  markedly longer (a rough guideline: more than ~30–40% longer, since German
  and French in particular tend to run longer than English) for a second
  pass — prefer a shorter, equally correct synonym or an abbreviation
  consistent with that language's UI conventions over a longer literal
  translation.
- Languages such as ja-JP, ko-KR, and zh-CN typically render shorter or
  comparable in character count but can be visually wider per character —
  keep translations concise rather than verbose even when the character
  count looks fine.
- If a control is genuinely too small for any faithful translation in a
  given language, say so explicitly in the report rather than silently
  truncating or picking a misleading translation — this is a UI change
  outside the skill's scope and needs the user's decision.

## Step 6 — Report

Summarize, per target project:

- Which strings were already translated and verified across all 13
  languages.
- Which strings were newly routed through the translation lookup call.
- Which keys were added to `translation.json`, and for which languages.
- Any translations flagged as too long for their control, with the reason.
- Which strings were intentionally left untranslated (proper noun,
  technical/debug-only, etc.) and why.
- Whether a **rebuild** or asset re-bundle is required for the change to
  take effect, based on what Step 1 found about how the file is loaded.

## Do NOT translate (proper nouns & non-text tokens)

Leave these as literals — adapt this list to the target project, but the
categories are generally:

- Application / brand and product names, edition/tier names.
- Model codes, SKUs, API identifiers, version strings.
- Aspect ratios, resolutions (`16:9`, `1080p`, `4k`), and other standardized
  technical values.
- Purely numeric UI items (durations, counts) unless the number is embedded
  in a translatable sentence.
- File-dialog filter strings and file extensions (`*.mp4`, `*.png`, …) — OS
  filter syntax, not UI labels, unless the user explicitly asks to localize
  the human-readable part of the filter description.
- API / structured-protocol tokens, JSON field names, status markers like
  `[SUCCESS]`/`[ERROR]`/`[WARNING]`, log text, and debug-output strings.

Before starting on a new project, confirm this exclusion list with anything
project-specific the user calls out (their own brand names, model codes,
etc.) in addition to the general categories above.

## Hard constraints

- Only routing UI text through the translation call and editing
  `translation.json` are in scope. Do not refactor unrelated code, change
  layout/behavior, or alter generation/business logic.
- Keep the English source string and the JSON key byte-for-byte identical.
- A new key must be added to **every** one of the 13 language blocks, in the
  fixed order specified above.
- `translation.json` must stay validly formatted per the encoding/format
  discovered in Step 1.
- Respect the length constraints in Step 5 — do not ship a translation that
  is known to overflow its control without flagging it.
- After editing the JSON, tell the user whether a rebuild/re-bundle is
  required, per Step 1's findings.
- **Never** perform any version-control action. Leave all changes
  uncommitted for the user to review.
