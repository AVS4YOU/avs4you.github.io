# Plugin Manifest (`plugins.json`)

`https://avs4you.github.io/plugins.json` is the machine-readable catalogue of
the marketplace: one record per plugin, with absolute download links for both
architectures. `package.py` generates it from every `plugins/*/config.json`,
together with the data blob inlined into `index.html`. Never edit it by hand -
edit the `config.json` and run `python package.py`.

The file is a **public contract**. Read the rules below before renaming a
field, removing a plugin or changing how packages are published.

## Who reads it

- **AVS4YOU installers** (Video Converter, Video Editor, Image Converter, Media
  Player, Install Pack). During setup they download the manifest, look up the
  sample plugins build_tools configured for the product (`<module>-plugins` in
  build_tools `defaults`, e.g. `effect-vhs, veo3`) and download the package
  for their own architecture. They parse the file with a small hand-written
  JSON reader in Inno Setup Pascal, and an installer that has shipped is never
  updated - whatever it expects has to keep working.
- **The storefront** carries the same records inline in `index.html` and does
  not fetch this file.
- Anything else that needs a download link should read this file instead of
  scraping the page.

## Format

```json
{
  "schema": 1,
  "site": "https://avs4you.github.io/",
  "plugins": [
    {
      "slug": "effect-vhs",
      "pluginId": "EffectVHS.plugin",
      "name": "Effect VHS",
      "desc": "Authentic VHS tape effect with noise, color degradation, ...",
      "apps": [ "Video Converter", "Video Editor", "Image Converter" ],
      "version": "v1.0.1",
      "type": "effect",
      "typeLabel": "Image Effect",
      "aiPowered": false,
      "requiresKey": false,
      "tint": "linear-gradient(135deg,#E0398E,#7C2BD6)",
      "media": "plugins/effect-vhs/Effect VHS.gif",
      "basePath": "plugins/effect-vhs",
      "downloads": {
        "x86": "https://github.com/AVS4YOU/avs4you.github.io/releases/download/effect-vhs-v1.0.1/effect-vhs-v1.0.1-x86.avsp",
        "x64": "https://github.com/AVS4YOU/avs4you.github.io/releases/download/effect-vhs-v1.0.1/effect-vhs-v1.0.1-x64.avsp"
      }
    }
  ]
}
```

| Field | Installers need it | Comes from | Rule |
|---|---|---|---|
| `schema` | yes | `MANIFEST_SCHEMA` in `package.py` | integer, currently `1` |
| `site` | no | `SITE_URL` in `package.py` | base for `basePath` and `media` |
| `plugins[].slug` | yes | `config.json` | `^[a-z0-9][a-z0-9-]*$`, unique ignoring case |
| `plugins[].pluginId` | yes | `config.json` | `^[A-Za-z0-9][A-Za-z0-9._-]*$`, ends in `.plugin`, no `..`, at most 128 characters, unique ignoring case, identical to `PluginId()` |
| `plugins[].version` | no | `config.json` | `^[A-Za-z0-9._-]+$`, no `..`, does not end in `.` or `.lock` - it is part of the download URLs and of the release tag |
| `plugins[].downloads.x86`, `.x64` | yes | `release.asset_url()` | absolute GitHub release URL, ASCII, no spaces or quotes, at most 1024 characters |
| everything else | no | `config.json`, `package.py` | storefront only, free to change within the size limits below |

- **`slug`** is the name build_tools lists and the file name the installers
  cache a package under (`<slug>.avsp`).
- **`pluginId`** is the string the plugin DLL's `PluginId()` returns, and the
  folder the plugin is installed into: `%APPDATA%\AVS4YOU\Plugins\<pluginId>`,
  or `Plugins-x64` for 64-bit hosts. Installers check that folder to tell
  whether the plugin is already installed, so a `pluginId` that drifts from the
  code makes them offer the plugin again on every run. Keep the `<Name>.plugin`
  convention - installers only accept ids that end in `.plugin`, do not
  contain `..` and are at most 128 characters long, and `package.py` refuses
  any other. The id cannot be derived from the slug
  (`effect-restoration-gaterv3` is `EffectGaterv3.plugin`), and its case is
  whatever the code uses (`heygen.plugin`, `Youtube.plugin`).
- **Encoding**: pure ASCII - `package.py` writes it with `ensure_ascii=True`,
  so non-ASCII text in `name` or `desc` arrives as `\uXXXX`. UTF-8 without a
  BOM, LF line ends. `\"`, `\\` and `\n` can still appear in any string, so a
  reader has to handle escapes even in fields it skips.
- **Readers must not depend on** key order, whitespace or the absence of
  unknown fields.
- **Size limits** of the installers' reader: the whole file at most 1 MiB
  (1,048,576 characters), at most 16384 values - every string, number,
  `true`, `false`, `null` and array counts as one - and at most 32 levels of
  nested objects and arrays. A single string may be of any length. Going over
  a limit makes every installer reject the whole manifest: nothing is
  downloaded (packages already in an installer's cache are still installed).
  Today's file is about 10 KB with 195 values, nested 4 levels deep.

## What installers do with it

The installer side lives in app-main-2010,
`Common/InnoSetup/plugin-samples-code.iss`.

- `schema` other than `1`: no downloads; packages already in the installer's
  cache are still installed.
- Take the first `plugins[k]` whose `slug` matches. A slug missing from the
  manifest is skipped and logged; no error is shown.
- Drop a `pluginId` that is missing, has characters outside `[A-Za-z0-9._-]`,
  starts with `.`, does not end in `.plugin`, contains `..` or is longer than
  128 characters, and log it. The plugin is still downloaded and installed,
  but without an id the installer cannot see that it is already installed,
  so it offers the plugin again on every run.
- Accept `downloads.<arch>` only if it starts with
  `https://github.com/AVS4YOU/`, the rest is `[A-Za-z0-9-_./]` without `..` or
  `//`, it ends in `.avsp` and is at most 1024 characters long.
- Download it over https with urlmon (`URLDownloadToFile`: WinInet, the IE
  proxy settings), which follows GitHub's redirect to the signed asset URL.
  Not with isxdl: it keeps the ~930-character redirect target in a fixed
  buffer and corrupts memory (the next file of a batch fails, a single file can
  crash the setup).
- If `%APPDATA%\AVS4YOU\Plugins[-x64]\<pluginId>` exists, the plugin is
  installed and not offered.
- A package that cannot be downloaded (404, network) is skipped - after a
  network failure the remaining ones are not tried. The setup continues, and
  unless it runs silently it tells the user that this does not affect the
  installation and plugins can be downloaded later from the program.
- If the manifest cannot be downloaded, only packages already in the
  installer's cache are installed.

## Changing the format

- **Adding a field** - top level or per plugin - never bumps `schema`.
- **Renaming, removing or changing the type of** `schema`, `plugins`, `slug`,
  `pluginId`, `downloads.x86` or `downloads.x64`, or changing the top-level
  shape, bumps `MANIFEST_SCHEMA`. Every installer already shipped then sees a
  schema it does not know and stops offering plugins, so treat a bump as a last
  resort: prefer adding a new field next to the old one.

## Publishing rules

- **Release before you push.** `package.py` writes the URL of the new version
  whether or not the release exists. Run `python release.py <plugin>` first,
  then `python package.py`, then push - otherwise installers and the storefront
  get a 404 until the release is created.
- **Never remove or rename a slug that a shipped default list names**, and
  never drop one of its architectures. Today that is `effect-vhs` and `veo3`
  (build_tools `defaults`: `effect-vhs, veo3` for Video Converter, Video
  Editor, Media Player and Install Pack, `effect-vhs` for Image Converter).
- **Keep `pluginId` equal to `PluginId()`.** `tools/plugin-tools/new_effect.py`
  writes both from one value and `check_effect.py` compares them for effect
  plugins. Content plugins have no scaffolder, so their `pluginId` is added to
  `config.json` by hand. `release.py` refuses to publish a package unless one of
  its DLLs contains the id (as the UTF-16 string `PluginId()` returns).
- **Keep releases under `https://github.com/AVS4YOU/`** (`RELEASE_REPO` in
  `release.py`); installers reject any other host.
- **Publish a plain ZIP.** Before caching a downloaded `.avsp`, installers
  check for the local file header `PK\3\4` at offset 0 and the end of
  central directory record `PK\5\6` exactly 22 bytes before the end of the
  file. A package with an archive comment or with anything in front of the
  first entry (a self-extracting stub, for example) is thrown away as a
  failed download. Python's `zipfile`, which `build_effect.py` uses, writes
  this layout; check a package made with another archiver.

## Validation

`package.py` stops with an error naming the offending `config.json` when:

- `slug`, `pluginId` or `version` is missing, is not a string or breaks the
  rules in the table above (`release.py:check_identity`, which `release.py`
  itself also applies);
- a download URL is not plain ASCII, contains spaces or quotes, or is longer
  than 1024 characters;
- two plugins share a `slug` or a `pluginId`, compared ignoring case.

## Legacy package paths

Installers shipped before manifest support do not read `plugins.json`. They
download two packages straight from Pages:

- `plugins/effect-vhs/build/x86/effect-vhs.avsp`
- `plugins/sora2/build/x86/Sora2.avsp`

Both are force-tracked (`git add -f`) although `build/` is ignored. Refresh
them whenever effect-vhs or sora2 is re-released, and remove them only once
those installers are out of circulation.
