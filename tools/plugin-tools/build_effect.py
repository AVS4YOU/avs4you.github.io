#!/usr/bin/env python3
"""Build, package and preview an AVS4YOU effect plugin.

One command takes a plugin source folder all the way to a marketplace entry:

    python tools/plugin-tools/build_effect.py effect-snow

Steps
  1. locate MSBuild through vswhere
  2. build Release|Win32 and Release|x64 from the plugin's .vcxproj
  3. zip each DLL into build/<arch>/<slug>.avsp   (an .avsp is a plain ZIP)
  4. build tools/PreviewGenerator if needed and render the preview GIF
  5. run package.py so index.html picks the plugin up

Useful flags:
  --arch x86|x64|both      default both
  --no-preview             skip the GIF
  --no-package             skip package.py
  --preview-input FILE     source frame for the GIF (default tools/PreviewGenerator/img/PreviewEffect.png)
  --fps N                  GIF frame rate, default 25
  --duration SECONDS       one forward pass of the GIF, default 2
  --side-by-side           render a before/after wipe instead of a plain loop
  --ping-pong              loop the GIF forwards then backwards
  --force-completeness V   pin dCompleteness while frames keep advancing
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLUGINS_DIR = ROOT / "plugins"
PREVIEW_DIR = ROOT / "tools" / "PreviewGenerator"
DEFAULT_PREVIEW_INPUT = PREVIEW_DIR / "img" / "PreviewEffect.png"

VSWHERE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) \
    / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"

ARCH = {
    # name: (msbuild platform, default OutDir relative to the project)
    "x86": ("Win32", "Release"),
    "x64": ("x64", os.path.join("x64", "Release")),
}


def fail(message: str) -> "NoReturn":  # type: ignore[valid-type]
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def run(command: list[str], cwd: Path | None = None) -> None:
    printable = " ".join(f'"{c}"' if " " in c else c for c in command)
    print(f"> {printable}")
    result = subprocess.run(command, cwd=str(cwd) if cwd else None)
    if result.returncode != 0:
        fail(f"command failed with exit code {result.returncode}")


def find_msbuild() -> str:
    override = os.environ.get("MSBUILD")
    if override and Path(override).exists():
        return override

    if VSWHERE.exists():
        # The C++ workload matters: a Build Tools install without it still has
        # MSBuild.exe but no Microsoft.Cpp.*.props, and the build then dies on
        # MSB4019 halfway through.
        query = [
            str(VSWHERE), "-latest", "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath",
        ]
        found = subprocess.run(query, capture_output=True, text=True)
        for line in found.stdout.splitlines():
            base = Path(line.strip())
            if not base.exists():
                continue
            for rel in (r"MSBuild\Current\Bin\MSBuild.exe", r"MSBuild\15.0\Bin\MSBuild.exe"):
                candidate = base / rel
                if candidate.exists():
                    return str(candidate)

    on_path = shutil.which("msbuild")
    if on_path:
        return on_path

    fail("MSBuild not found. Install Visual Studio Build Tools or set %MSBUILD%.")


def find_cmake() -> str:
    on_path = shutil.which("cmake")
    if on_path:
        return on_path

    for candidate in (
        Path(r"C:\Program Files\CMake\bin\cmake.exe"),
        Path(r"C:\Program Files (x86)\CMake\bin\cmake.exe"),
    ):
        if candidate.exists():
            return str(candidate)

    fail("cmake not found; needed only for the preview GIF (use --no-preview to skip).")


def resolve_project(plugin_dir: Path) -> Path:
    projects = sorted(plugin_dir.glob("*.vcxproj"))
    if not projects:
        fail(f"no .vcxproj in {plugin_dir}")
    if len(projects) > 1:
        preferred = [p for p in projects if p.stem.lower() == "effect"]
        if preferred:
            return preferred[0]
        fail(f"several .vcxproj files in {plugin_dir}; keep one or name it Effect.vcxproj")
    return projects[0]


def read_config(plugin_dir: Path) -> dict:
    config_path = plugin_dir / "config.json"
    if not config_path.exists():
        fail(f"{config_path} is missing; the marketplace index is built from it")
    with config_path.open(encoding="utf-8") as handle:
        return json.load(handle)


def build_arch(msbuild: str, project: Path, arch: str) -> Path:
    platform, out_rel = ARCH[arch]
    out_dir = project.parent / out_rel

    run([
        msbuild,
        str(project),
        "/nologo",
        "/verbosity:minimal",
        "/p:Configuration=Release",
        f"/p:Platform={platform}",
        f"/p:OutDir={out_dir}{os.sep}",
    ])

    dlls = sorted(out_dir.glob("*.dll"))
    if not dlls:
        fail(f"build produced no DLL in {out_dir}")
    if len(dlls) > 1:
        fail(f"more than one DLL in {out_dir}: {[d.name for d in dlls]}")
    return dlls[0]


def pack_avsp(plugin_dir: Path, slug: str, arch: str, dll: Path, extras: list[Path]) -> Path:
    target_dir = plugin_dir / "build" / arch
    target_dir.mkdir(parents=True, exist_ok=True)
    target = target_dir / f"{slug}.avsp"

    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(dll, dll.name)
        for extra in extras:
            archive.write(extra, extra.name)

    size_kb = target.stat().st_size // 1024
    print(f"packed {target.relative_to(ROOT).as_posix()} ({size_kb} KB)")
    return target


def ensure_preview_generator(cmake: str) -> Path:
    # Win32, so it can load the x86 plugin DLL.
    build_dir = PREVIEW_DIR / "build"
    exe = build_dir / "Release" / "PreviewGenerator.exe"
    if exe.exists():
        return exe

    if not (build_dir / "CMakeCache.txt").exists():
        run([cmake, "-S", str(PREVIEW_DIR), "-B", str(build_dir),
             "-G", "Visual Studio 16 2019", "-A", "Win32"])

    run([cmake, "--build", str(build_dir), "--config", "Release"])

    if not exe.exists():
        fail(f"PreviewGenerator was not produced at {exe}")
    return exe


def render_preview(exe: Path, dll: Path, plugin_dir: Path, config: dict, args) -> Path:
    media = config.get("media") or f"{config.get('name', plugin_dir.name)}.gif"
    target = plugin_dir / media
    target.parent.mkdir(parents=True, exist_ok=True)

    command = [str(exe), str(dll), str(args.preview_input), str(target),
               str(args.fps), "-duration", str(args.duration)]

    if args.side_by_side:
        command.append("-side-by-side")
    if args.ping_pong:
        command.append("-ping-pong")
    if args.force_completeness is not None:
        command += ["-force-completeness", str(args.force_completeness)]

    run(command)

    if not target.exists():
        fail(f"preview was not produced at {target}")

    size_kb = target.stat().st_size // 1024
    print(f"preview {target.relative_to(ROOT).as_posix()} ({size_kb} KB)")
    return target


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("plugin", help="plugin folder name under plugins/, e.g. effect-snow")
    parser.add_argument("--arch", choices=["x86", "x64", "both"], default="both")
    parser.add_argument("--no-preview", action="store_true")
    parser.add_argument("--no-package", action="store_true")
    parser.add_argument("--preview-input", type=Path, default=DEFAULT_PREVIEW_INPUT)
    parser.add_argument("--fps", type=int, default=25)
    parser.add_argument("--duration", type=float, default=2.0)
    parser.add_argument("--side-by-side", action="store_true")
    parser.add_argument("--ping-pong", action="store_true")
    parser.add_argument("--force-completeness", type=float, default=None)
    parser.add_argument("--extra", type=Path, nargs="*", default=[],
                        help="additional files to place inside the .avsp (models, DLLs, ...)")
    args = parser.parse_args()

    plugin_dir = (PLUGINS_DIR / args.plugin) if not Path(args.plugin).is_dir() else Path(args.plugin)
    plugin_dir = plugin_dir.resolve()
    if not plugin_dir.is_dir():
        fail(f"no such plugin folder: {plugin_dir}")

    config = read_config(plugin_dir)
    slug = config.get("slug") or plugin_dir.name
    project = resolve_project(plugin_dir)

    msbuild = find_msbuild()
    print(f"msbuild: {msbuild}")

    targets = ["x86", "x64"] if args.arch == "both" else [args.arch]
    built: dict[str, Path] = {}

    for arch in targets:
        print(f"\n=== {slug}: Release|{ARCH[arch][0]} ===")
        dll = build_arch(msbuild, project, arch)
        pack_avsp(plugin_dir, slug, arch, dll, list(args.extra))
        built[arch] = dll

    if not args.no_preview:
        print(f"\n=== {slug}: preview ===")
        if "x86" not in built:
            print("skipping preview: needs the x86 build (PreviewGenerator is Win32)")
        elif not args.preview_input.exists():
            fail(f"preview input not found: {args.preview_input}")
        else:
            exe = ensure_preview_generator(find_cmake())
            render_preview(exe, built["x86"], plugin_dir, config, args)

    if not args.no_package:
        print("\n=== index.html ===")
        run([sys.executable, str(ROOT / "package.py")], cwd=ROOT)

    print(f"\ndone: {slug}")


if __name__ == "__main__":
    main()
