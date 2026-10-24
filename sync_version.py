"""Write each plugin's config.json "version" into its sources.

    python sync_version.py                 every plugin
    python sync_version.py veo3 heygen     just these (plugin folder names)
    python sync_version.py --check         change nothing, exit 1 if out of sync

config.json is the one place a version is bumped. This copies it into the two
places the DLL carries it:

  PluginVersion()   the string the host shows      "v1.0.2" -> L"1.0.2"
  version.h         INTVER / STRVER, the DLL's      "v1.0.2" -> 1,0,2 / "1,0,2"
                    VERSIONINFO resource

Every replacement below names its file and must match exactly once, so a
refactor that moves PluginVersion() fails here instead of being skipped.
Run it before build.py.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from release import PLUGINS_DIR, ReleaseError, all_plugin_names, load_plugin

ROOT = Path(__file__).resolve().parent

# Patterns work on bytes: the sources are a mix of UTF-8, UTF-8 with BOM and
# a legacy code page, and only the ASCII version digits are touched.

# PluginVersion() { return export_str(L"1.0.0"); } - over any line breaks,
# whatever the helper is called (export_str, ExportString, utils::wstr_to_ptr)
PLUGIN_VERSION = (
    "PluginVersion()",
    re.compile(rb'(PluginVersion\(\)\s*\{\s*return\s+[\w:]+\(L")([^"]*)("\))'),
    lambda version: version.encode("ascii"),
)
# #define INTVER 1,0,0
INTVER = (
    "INTVER",
    re.compile(rb"(#define\s+INTVER\s+)([0-9]+(?:\s*,\s*[0-9]+)*)()"),
    lambda version: version.replace(".", ",").encode("ascii"),
)
# #define STRVER "1,0,0"
STRVER = (
    "STRVER",
    re.compile(rb'(#define\s+STRVER\s+")([^"]*)(")'),
    lambda version: version.replace(".", ",").encode("ascii"),
)
VERSION_H = (INTVER, STRVER)

# Keyed by the folder under plugins/; paths are relative to it. A folder with
# a config.json but no entry here is reported, not skipped.
FILES = {
    "ai-subtitles": {
        "src/exports.cpp": (PLUGIN_VERSION,),
        "src/version.h": VERSION_H,
    },
    "effect-restoration-gaterv3": {
        "src/dllmain.cpp": (PLUGIN_VERSION,),
        "version.h": VERSION_H,
    },
    "effect-spiral": {
        "dllmain.cpp": (PLUGIN_VERSION,),
        "version.h": VERSION_H,
    },
    "effect-vhs": {
        "dllmain.cpp": (PLUGIN_VERSION,),
        "version.h": VERSION_H,
    },
    "heygen": {
        # src/IHeygenPlugin.cpp is what CMakeLists.txt builds; the
        # CHeygenPluginIntf.cpp in the plugin root is not compiled
        "src/IHeygenPlugin.cpp": (PLUGIN_VERSION,),
        "src/version.h": VERSION_H,
    },
    "nano-banana": {
        "src/exports.cpp": (PLUGIN_VERSION,),
        "version.h": VERSION_H,
    },
    "sora2": {
        "src/exports.cpp": (PLUGIN_VERSION,),
        "version.h": VERSION_H,
    },
    "stem-splitter": {
        "src/exports.cpp": (PLUGIN_VERSION,),
        "src/version.h": VERSION_H,
    },
    "voice-clone": {
        "src/exports.cpp": (PLUGIN_VERSION,),
        "src/version.h": VERSION_H,
    },
    "StableAudio3": {
        "src/exports.cpp": (PLUGIN_VERSION,),
        "src/version.h": VERSION_H,
    },
    "sunoapi": {
        "src/suno_exports.cpp": (PLUGIN_VERSION,),
        "version.h": VERSION_H,
    },
    "Supertonic3": {
        "Supertonic3/exports.cpp": (PLUGIN_VERSION,),
        "Supertonic3/version.h": VERSION_H,
    },
    "veo3": {
        "src/exports.cpp": (PLUGIN_VERSION,),
        "src/version.h": VERSION_H,
    },
    "youtube": {
        "dllmain.cpp": (PLUGIN_VERSION,),
        "src/version.h": VERSION_H,
    },
}

# what VERSIONINFO can hold: up to four 16-bit numbers
NUMERIC_VERSION = re.compile(r"v?([0-9]{1,5}(?:\.[0-9]{1,5}){0,3})")


class SyncError(Exception):
    """A version or a source that this script cannot handle."""


def code_version(config_version: str, where: str) -> str:
    match = NUMERIC_VERSION.fullmatch(config_version)
    if not match or any(int(part) > 65535 for part in match.group(1).split(".")):
        raise SyncError(f"{where}: version {config_version!r} is not 'v' plus up to four "
                        f"dot-separated numbers - VERSIONINFO cannot hold it")
    return match.group(1)


def sync_file(path: Path, replacements: tuple, version: str, check: bool) -> list[str]:
    """Return a line per value that differs; write the file unless check."""

    if not path.is_file():
        raise SyncError(f"{path.relative_to(ROOT).as_posix()} does not exist")
    data = path.read_bytes()
    changes = []
    for name, pattern, render in replacements:
        matches = list(pattern.finditer(data))
        if len(matches) != 1:
            raise SyncError(f"{path.relative_to(ROOT).as_posix()}: {name} found "
                            f"{len(matches)} times, expected once")
        old, new = matches[0].group(2), render(version)
        if old != new:
            changes.append(f"{name} {old.decode('ascii', 'replace')} -> {new.decode('ascii')}")
            data = pattern.sub(lambda match: match.group(1) + new + match.group(3), data)
    if changes and not check:
        path.write_bytes(data)
    return changes


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("plugins", nargs="*", metavar="PLUGIN",
                        help="plugin folder names (default: all)")
    parser.add_argument("--check", action="store_true",
                        help="change nothing; exit 1 if any source is out of sync")
    args = parser.parse_args(argv)

    folders = all_plugin_names()
    unknown = [name for name in folders if name not in FILES]
    if unknown:
        print(f"error: no entry in sync_version.py for: {', '.join(unknown)}", file=sys.stderr)
        return 1

    if args.plugins:
        by_lower = {name.lower(): name for name in folders}
        missing = [name for name in args.plugins if name.lower() not in by_lower]
        if missing:
            print(f"error: no such plugin folder: {', '.join(missing)}", file=sys.stderr)
            return 1
        folders = [by_lower[name.lower()] for name in args.plugins]

    out_of_sync = 0
    try:
        for folder in folders:
            plugin = load_plugin(folder)
            version = code_version(plugin["version"], f"plugins/{folder}/config.json")
            for relative, replacements in FILES[folder].items():
                path = PLUGINS_DIR / folder / relative
                changes = sync_file(path, replacements, version, args.check)
                shown = path.relative_to(ROOT).as_posix()
                if changes:
                    out_of_sync += 1
                    verb = "differs" if args.check else "updated"
                    print(f"{verb}: {shown}: {'; '.join(changes)}")
                else:
                    print(f"ok:      {shown} ({version})")
    except (SyncError, ReleaseError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if args.check and out_of_sync:
        print(f"\n{out_of_sync} file(s) out of sync - run: python sync_version.py", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
