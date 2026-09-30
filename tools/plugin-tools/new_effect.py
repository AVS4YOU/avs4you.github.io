#!/usr/bin/env python3
"""Scaffold a new AVS4YOU effect plugin.

    python tools/plugin-tools/new_effect.py effect-snow \
        --name "Effect Snow" --effect-name Snow \
        --desc "Drifting snowfall with depth layers and wind." \
        --apps "Video Converter,Video Editor,Image Converter" \
        --tint "linear-gradient(135deg,#7FB3FF,#E9F3FF)"

Creates plugins/<slug>/ with dllmain.cpp, the Visual Studio project, module.def
and config.json, all wired to the shared SDK. The generated dllmain.cpp already
implements the temporally coherent particle pattern - replace the Draw() body
with the actual visuals, then run build_effect.py.
"""

from __future__ import annotations

import argparse
import io
import json
import re
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEMPLATES = Path(__file__).resolve().parent / "templates"
PLUGINS_DIR = ROOT / "plugins"

# Marketplace app label -> AVSConsts.h macro.
APP_IDS = {
    "video converter": "AVS_VIDEO_CONVERTER",
    "video editor": "AVS_VIDEO_EDITOR",
    "image converter": "AVS_IMAGE_CONVERTER",
    "photo editor": "AVS_PHOTO_EDITOR",
    "video remaker": "AVS_VIDEO_REMAKER",
    "audio editor": "AVS_AUDIO_EDITOR",
    "audio converter": "AVS_AUDIO_CONVERTER",
}

DEFAULT_APPS = "Video Converter,Video Editor,Image Converter"
DEFAULT_TINT = "linear-gradient(135deg,#3B4FE0,#7C5CFF)"


def fail(message: str):
    raise SystemExit(f"ERROR: {message}")


def render(template_name: str, values: dict) -> str:
    text = io.open(TEMPLATES / template_name, encoding="utf-8-sig").read()
    for key, value in values.items():
        text = text.replace("{{" + key + "}}", value)

    leftover = re.findall(r"\{\{[A-Z_]+\}\}", text)
    if leftover:
        fail(f"{template_name} still has unfilled placeholders: {sorted(set(leftover))}")

    return text


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    io.open(path, "w", encoding="utf-8", newline="\r\n").write(text)
    print(f"created {path.relative_to(ROOT).as_posix()}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("slug", help="folder and marketplace slug, e.g. effect-snow")
    parser.add_argument("--name", help='marketplace card title, e.g. "Effect Snow"')
    parser.add_argument("--effect-name", help="name shown in the host effect list, e.g. Snow")
    parser.add_argument("--desc", default="", help="one-line marketplace description")
    parser.add_argument("--apps", default=DEFAULT_APPS,
                        help=f"comma separated host apps (default: {DEFAULT_APPS})")
    parser.add_argument("--tint", default=DEFAULT_TINT, help="CSS gradient for the card")
    parser.add_argument("--force", action="store_true", help="overwrite an existing folder")
    args = parser.parse_args()

    slug = args.slug.strip()
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", slug):
        fail("slug must be lowercase letters, digits and dashes, e.g. effect-snow")

    plugin_dir = PLUGINS_DIR / slug
    if plugin_dir.exists() and not args.force:
        fail(f"{plugin_dir} already exists (pass --force to overwrite)")

    bare = re.sub(r"^effect-", "", slug)
    effect_name = args.effect_name or bare.replace("-", " ").title()
    display_name = args.name or f"Effect {effect_name}"
    project_name = re.sub(r"[^A-Za-z0-9]", "", bare).lower() or "effect"
    plugin_id = "Effect" + re.sub(r"[^A-Za-z0-9]", "", effect_name.title()) + ".plugin"
    # the install folder of the plugin: it must not clash with another one (NTFS ignores case)
    for other in sorted(PLUGINS_DIR.glob("*/config.json")):
        if other.parent == plugin_dir:
            continue
        try:
            other_id = json.loads(other.read_text(encoding="utf-8")).get("pluginId", "")
        except (OSError, ValueError):
            continue
        if isinstance(other_id, str) and other_id.lower() == plugin_id.lower():
            fail(f"pluginId {plugin_id} is already used by {other.parent.name} - pass another --effect-name")
    exports_define = "EFFECT" + re.sub(r"[^A-Za-z0-9]", "", bare).upper() + "_EXPORTS"

    apps = [a.strip() for a in args.apps.split(",") if a.strip()]
    if not apps:
        fail("--apps must list at least one host app")

    cases = []
    for app in apps:
        macro = APP_IDS.get(app.lower())
        if not macro:
            fail(f"unknown app '{app}'. Known: {', '.join(sorted(APP_IDS))}")
        cases.append(f"        case {macro}:")

    project_guid = str(uuid.uuid4()).upper()
    solution_guid = str(uuid.uuid4()).upper()

    values = {
        "SLUG": slug,
        "NAME": display_name,
        "EFFECT_NAME": effect_name,
        "DESC": args.desc or f"{effect_name} effect.",
        "PLUGIN_ID": plugin_id,
        "PROJECT_NAME": project_name,
        "EXPORTS_DEFINE": exports_define,
        "PROJECT_GUID": "{" + project_guid + "}",
        "PROJECT_GUID_LOWER": "{" + project_guid.lower() + "}",
        "SOLUTION_GUID": "{" + solution_guid + "}",
        "APP_CASES": "\n".join(cases),
    }

    write(plugin_dir / "dllmain.cpp", render("dllmain.cpp.tmpl", values))
    write(plugin_dir / "Effect.vcxproj", render("Effect.vcxproj.tmpl", values))
    write(plugin_dir / "Effect.vcxproj.filters", render("Effect.vcxproj.filters.tmpl", values))
    write(plugin_dir / "module.def", render("module.def.tmpl", values))
    write(plugin_dir / f"{project_name}.sln", render("solution.sln.tmpl", values))

    config = {
        "slug": slug,
        "pluginId": plugin_id,
        "name": display_name,
        "desc": args.desc or f"{effect_name} effect.",
        "apps": apps,
        "version": "v1.0.0",
        "type": "effect",
        "typeLabel": "Image Effect",
        "aiPowered": False,
        "requiresKey": False,
        "tint": args.tint,
        "media": f"{display_name}.gif",
    }
    write(plugin_dir / "config.json",
          json.dumps(config, ensure_ascii=False, indent=2) + "\n")

    print(f"\nnext: edit plugins/{slug}/dllmain.cpp (the Draw() body), then")
    print(f"      python tools/plugin-tools/build_effect.py {slug}")


if __name__ == "__main__":
    main()
