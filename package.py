from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PLUGINS_DIR = ROOT / "plugins"
INDEX_HTML = ROOT / "index.html"
SCRIPT_ID = "plugins-data"


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

    for plugin_dir in sorted(PLUGINS_DIR.iterdir(), key=lambda item: item.name.lower()):
        if not plugin_dir.is_dir():
            continue

        config_path = plugin_dir / "config.json"
        if not config_path.exists():
            print(f"skip: {to_url(plugin_dir)} has no config.json")
            continue

        with config_path.open("r", encoding="utf-8") as file:
            config = json.load(file)

        base_path = to_url(plugin_dir)
        config["basePath"] = base_path
        config["media"] = join_plugin_path(base_path, config.get("media"))
        config["download"] = join_plugin_path(base_path, config.get("download"))
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


def main() -> None:
    plugins = load_plugins()
    write_index(make_script(plugins))
    print(f"packed {len(plugins)} plugins into {to_url(INDEX_HTML)}")


if __name__ == "__main__":
    main()
