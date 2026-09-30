from __future__ import annotations

import json
import sys
from pathlib import Path

from release import ARCHS, ReleaseError, asset_url, check_identity

ROOT = Path(__file__).resolve().parent
PLUGINS_DIR = ROOT / "plugins"
INDEX_HTML = ROOT / "index.html"
PLUGINS_JSON = ROOT / "plugins.json"
SCRIPT_ID = "plugins-data"

# The repository is named <owner>.github.io, so Pages serves it from the domain
# root. plugins.json carries it so a client can resolve the relative paths.
SITE_URL = "https://avs4you.github.io/"

# plugins.json is a public contract (docs/PluginsManifest.md): shipped
# installers read it and give up on any schema they do not know. Adding a field
# never bumps it. Renaming, removing or retyping slug, pluginId or
# downloads.x86/x64, or changing the top-level shape, does - and installers
# already in the field then stop offering plugins.
MANIFEST_SCHEMA = 1

# What the installers' parser takes (app-main Common/InnoSetup/plugin-preinstalled-code.iss,
# PPJ_MAX_TEXT / PPJ_MAX_LEAVES / PPJ_MAX_DEPTH). A bigger manifest is rejected whole and
# every shipped installer stops offering plugins.
MANIFEST_MAX_CHARS = 1048576
MANIFEST_MAX_VALUES = 16384
MANIFEST_MAX_DEPTH = 32

# the keys installers read, spelled the one way package.py and release.py use them
CANONICAL_KEYS = {key.lower(): key for key in ("slug", "pluginId", "version", "downloads")}


def to_url(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def join_plugin_path(base_path: str, value: str | None) -> str:
    if not value:
        return ""

    normalized = str(value).replace("\\", "/")
    if normalized.startswith(("http://", "https://", "data:")) or normalized.startswith("//"):
        return normalized

    return f"{base_path}/{normalized.lstrip('/')}"


def load_plugins() -> list[dict]:
    plugins: list[dict] = []
    # "slug:<value>" / "pluginId:<value>" -> the config that claimed it. Both end
    # up as names on disk (the <pluginId> install folder, the installers'
    # <slug>.avsp cache) and NTFS ignores case, so they are compared lowercased.
    owners: dict[str, str] = {}

    for plugin_dir in sorted(PLUGINS_DIR.iterdir(), key=lambda item: item.name.lower()):
        if not plugin_dir.is_dir():
            continue

        config_path = plugin_dir / "config.json"
        if not config_path.exists():
            print(f"skip: {to_url(plugin_dir)} has no config.json")
            continue

        with config_path.open("r", encoding="utf-8") as file:
            config = json.load(file)

        # json accepts NaN, Infinity and 1e400, JSON (and the installers' parser) does not
        try:
            json.dumps(config, allow_nan=False)
        except ValueError:
            sys.exit(f"error: {to_url(config_path)}: NaN, Infinity or a number too large "
                     f"for a float is not JSON - installers would reject plugins.json")

        # installers match keys ignoring case and take the first one, so a
        # "PluginID" next to "pluginId" would win there
        seen: dict[str, str] = {}
        for key in config:
            other = seen.setdefault(key.lower(), key)
            if other != key:
                sys.exit(f"error: {to_url(config_path)}: keys {other!r} and {key!r} "
                         f"differ only in case")
            canonical = CANONICAL_KEYS.get(key.lower())
            if canonical and key != canonical:
                sys.exit(f"error: {to_url(config_path)}: spell {key!r} as {canonical!r}")

        try:
            config.update(check_identity(config, to_url(config_path)))
        except ReleaseError as error:
            sys.exit(f"error: {error}")

        for key in ("slug", "pluginId"):
            owned = f"{key}:{config[key].lower()}"
            if owned in owners:
                sys.exit(
                    f"error: {to_url(config_path)}: '{key}' {config[key]!r} is "
                    f"already used by {owners[owned]} (compared ignoring case)"
                )
            owners[owned] = to_url(config_path)

        base_path = to_url(plugin_dir)
        config["basePath"] = base_path
        config["media"] = join_plugin_path(base_path, config.get("media"))

        # Packages are served from GitHub releases, never from Pages: Pages does
        # not resolve Git LFS pointers and large .avsp files would be handed to
        # the user as a 134-byte text file. release.py publishes the assets under
        # exactly these names.
        config["downloads"] = {
            arch: asset_url(config["slug"], config["version"], arch) for arch in ARCHS
        }
        plugins.append(config)

    return plugins


def make_script(plugins: list[dict]) -> str:
    data = json.dumps(plugins, ensure_ascii=False, indent=2)
    data = data.replace("</", "<\\/")
    return f'<script type="application/json" id="{SCRIPT_ID}">\n{data}\n</script>'


def write_index(script: str) -> None:
    html = INDEX_HTML.read_text(encoding="utf-8")
    start_marker = f'<script type="application/json" id="{SCRIPT_ID}">'

    if start_marker in html:
        start = html.index(start_marker)
        end = html.index("</script>", start) + len("</script>")
        html = html[:start] + script + html[end:]
    else:
        main_script = "  <script>"
        if main_script not in html:
            raise RuntimeError("Could not find main <script> tag in index.html")
        html = html.replace(main_script, "  " + script + "\n\n" + main_script, 1)

    INDEX_HTML.write_text(html, encoding="utf-8", newline="")


def count_values(value, depth: int = 0) -> tuple[int, int]:
    """(stored values, deepest level) the way the installers' parser counts
    them: every scalar, plus one length entry per array."""

    if isinstance(value, dict):
        items, own = list(value.values()), 0
    elif isinstance(value, list):
        items, own = value, 1
    else:
        return 1, depth
    values, deepest = own, depth
    for item in items:
        item_values, item_depth = count_values(item, depth + 1)
        values += item_values
        deepest = max(deepest, item_depth)
    return values, deepest


def make_manifest(plugins: list[dict]) -> str:
    """Publish the same records index.html carries, as a readable file.

    Anything that resolves a download without scraping the storefront reads
    https://avs4you.github.io/plugins.json instead. "downloads" holds the
    absolute release URLs; "basePath" and "media" resolve against "site".

    Written as pure ASCII: the installers' parser only has to understand
    \\uXXXX escapes, never a multi-byte encoding.
    """

    manifest = {
        "schema": MANIFEST_SCHEMA,
        "site": SITE_URL,
        "plugins": plugins,
    }
    data = json.dumps(manifest, ensure_ascii=True, indent=2, allow_nan=False) + "\n"

    values, deepest = count_values(manifest)
    if len(data) > MANIFEST_MAX_CHARS:
        sys.exit(f"error: {to_url(PLUGINS_JSON)} would be {len(data)} characters, "
                 f"installers read at most {MANIFEST_MAX_CHARS}")
    if values > MANIFEST_MAX_VALUES:
        sys.exit(f"error: {to_url(PLUGINS_JSON)} would hold {values} values, "
                 f"installers read at most {MANIFEST_MAX_VALUES}")
    if deepest > MANIFEST_MAX_DEPTH:
        sys.exit(f"error: {to_url(PLUGINS_JSON)} would nest {deepest} levels deep, "
                 f"installers read at most {MANIFEST_MAX_DEPTH}")
    return data


def main() -> None:
    plugins = load_plugins()
    # checked before anything is written, so a refused manifest leaves both files alone
    manifest = make_manifest(plugins)
    write_index(make_script(plugins))
    PLUGINS_JSON.write_text(manifest, encoding="utf-8", newline="")
    print(
        f"packed {len(plugins)} plugins into "
        f"{to_url(INDEX_HTML)} and {to_url(PLUGINS_JSON)}"
    )


if __name__ == "__main__":
    main()
