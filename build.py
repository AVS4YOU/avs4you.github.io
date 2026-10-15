"""Build every plugin and pack its .avsp where release.py looks for it.

    python build.py                        every plugin, x86 and x64
    python build.py veo3 heygen            just these (plugin folder names)
    python build.py --arch x64 --clean

For each plugin:

  1. plugins/<folder>/fetch.py, if there is one, gets the third-party code git
     does not hold - NuGet packages, sa3.cpp and the backends built from it.
     It gets --arch and --clean passed on.
  2. The plugin's own project is built Release with Visual Studio 2019 -
     MSBuild for a .vcxproj, CMake for heygen and StableAudio3.
  3. Its DLL and runtime files are zipped into build/<arch>/<name>.avsp and
     checked the way release.py checks them.

Versions and publishing stay separate steps:

    python release.py --all            then            python package.py

The build is incremental, and a fetch.py leaves alone what it already has;
--clean rebuilds everything, third-party backends included.

A plugin that fails does not stop the others; the run exits 1 at the end.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from release import (ARCHS, PLUGINS_DIR, ReleaseError, all_plugin_names, load_plugin,
                     package_has_plugin_id, package_is_plain_zip)

ROOT = Path(__file__).resolve().parent

VSWHERE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) \
    / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"

PLATFORM = {"x86": "Win32", "x64": "x64"}
NUGET_RID = {"x86": "win-x86", "x64": "win-x64"}


class BuildError(Exception):
    """A step failed; the message says which and why."""


# --- tools ------------------------------------------------------------------


@dataclass
class Tools:
    vs_path: Path
    msbuild: Path
    _cmake: str | None = None

    @property
    def cmake(self) -> str:
        if self._cmake is None:
            self._cmake = find_cmake(self.vs_path)
        return self._cmake


def find_vs2019() -> Tools:
    # The C++ workload matters: a Build Tools install without it has
    # MSBuild.exe but no Microsoft.Cpp.props, and the build dies on MSB4019.
    if not VSWHERE.exists():
        raise BuildError(f"vswhere not found at {VSWHERE} - is Visual Studio 2019 installed?")
    found = subprocess.run(
        [str(VSWHERE), "-products", "*", "-version", "[16.0,17.0)",
         "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"],
        capture_output=True, text=True,
    )
    for line in found.stdout.splitlines():
        vs_path = Path(line.strip())
        msbuild = vs_path / "MSBuild" / "Current" / "Bin" / "MSBuild.exe"
        if msbuild.exists():
            return Tools(vs_path, msbuild)
    raise BuildError("no Visual Studio 2019 with the C++ workload (VC.Tools.x86.x64) found")


def find_cmake(vs_path: Path) -> str:
    candidates = [
        shutil.which("cmake"),
        r"C:\Program Files\CMake\bin\cmake.exe",
        str(vs_path / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake"
            / "CMake" / "bin" / "cmake.exe"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).exists():
            return candidate
    raise BuildError("cmake not found (heygen and StableAudio3 need it)")


def run(command: list[str], cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    printable = " ".join(f'"{part}"' if " " in part else part for part in command)
    print(f"> {printable}", flush=True)
    result = subprocess.run(command, cwd=str(cwd) if cwd else None,
                            env={**os.environ, **env} if env else None)
    if result.returncode != 0:
        raise BuildError(f"command failed with exit code {result.returncode}: {command[0]}")


# --- builders -----------------------------------------------------------------
# A builder compiles one architecture and returns what goes into the package:
# {name inside the .avsp: file or directory on disk}. Third-party files it
# packs come ready-made: from git, or from the plugin's fetch.py.

Content = dict[str, Path]
Builder = Callable[[Path, str, Tools, bool], Content]


def msbuild(project: str, dll: str, extras: dict[str, str] | None = None,
            props: dict[str, str] | None = None, output: str | None = None) -> Builder:
    """A .vcxproj plugin. OutDir is redirected to build/stage/<arch>, so a
    project whose own OutDir points at %APPDATA% or at build/<arch> cannot
    leave a stray DLL next to the package. A project that pins the linker's
    OutputFile ignores OutDir; output names where its DLL really lands. In
    extras and output, {arch} and {rid} expand to x86/x64 and win-x86/win-x64;
    {stage} is that OutDir."""

    def build(plugin_dir: Path, arch: str, tools: Tools, clean: bool) -> Content:
        project_path = plugin_dir / project
        stage = plugin_dir / "build" / "stage" / arch
        shutil.rmtree(stage, ignore_errors=True)
        stage.mkdir(parents=True)

        # forward slashes: a trailing backslash would escape the closing quote
        expand = {"arch": arch, "rid": NUGET_RID[arch], "stage": stage.as_posix()}
        command = [
            str(tools.msbuild), str(project_path), "/nologo", "/m", "/verbosity:minimal",
            "/t:Rebuild" if clean else "/t:Build",
            "/p:Configuration=Release", f"/p:Platform={PLATFORM[arch]}",
            f"/p:OutDir={stage.as_posix()}/",
        ]
        command += [f"/p:{key}={value.format(**expand)}" for key, value in (props or {}).items()]
        run(command, cwd=plugin_dir)

        built = plugin_dir / output.format(**expand) if output else stage / dll
        content = {dll: built}
        for name, source in (extras or {}).items():
            content[name] = plugin_dir / source.format(**expand)
        return content

    return build


def cmake(binary_dir: str, output: str, dll: str, defines: dict[str, str] | None = None,
          extras: dict[str, str | dict[str, str]] | None = None) -> Builder:
    """A CMake plugin, configured into its own binary_dir under the plugin.
    output is where CMakeLists.txt puts the DLL. In paths, {arch} and
    {platform} expand to x86/x64 and Win32/x64; an extras value may instead
    map an architecture to a path, for files only one architecture packs."""

    def build(plugin_dir: Path, arch: str, tools: Tools, clean: bool) -> Content:
        expand = {"arch": arch, "platform": PLATFORM[arch]}
        binary = plugin_dir / binary_dir.format(**expand)
        if clean:
            shutil.rmtree(binary, ignore_errors=True)
        run([tools.cmake, "-S", str(plugin_dir), "-B", str(binary),
             "-G", "Visual Studio 16 2019", "-A", PLATFORM[arch],
             f"-DCMAKE_GENERATOR_INSTANCE={tools.vs_path}",
             *[f"-D{key}={value}" for key, value in (defines or {}).items()]])
        run([tools.cmake, "--build", str(binary), "--config", "Release", "--", "/m"])

        content = {dll: plugin_dir / output.format(**expand)}
        for name, source in (extras or {}).items():
            if isinstance(source, dict):
                if arch not in source:
                    continue
                source = source[arch]
            content[name] = plugin_dir / source.format(**expand)
        return content

    return build


# --- what each plugin is -----------------------------------------------------
# Keyed by the folder under plugins/. The package layout matches what the
# plugin loads at runtime; a folder with a config.json but no entry here is
# reported, not skipped.

PLUGINS: dict[str, Builder] = {
    # fetch.py builds whisper-cli for AVX2 CPUs and a compat one; ffmpeg pulls
    # the audio out of video and is the one the YouTube plugin ships
    "ai-subtitles": cmake(
        "build/cmake-{arch}", "build/{arch}/AiSubtitles.dll", "AiSubtitles.dll",
        extras={"ffmpeg.exe": "../youtube/resources/x86/ffmpeg.exe",
                "whisper-cli.exe": "build/{arch}/whisper-cli.exe",
                "whisper-cli-compat.exe": "build/{arch}/whisper-cli-compat.exe",
                "whisper-cli-vulkan.exe": {"x64": "build/x64/whisper-cli-vulkan.exe"}}),
    "effect-restoration-gaterv3": msbuild(
        "gaterv3.vcxproj", "gaterv3.dll",
        extras={"onnxruntime.dll": "libs/{arch}/onnxruntime.dll",
                "1xgaterv3_r_restore_fp32_op17.onnx": "3dparty/1xgaterv3_r_restore_fp32_op17.onnx"}),
    "effect-spiral": msbuild("Effect.vcxproj", "Effect.dll"),
    "effect-vhs": msbuild("Effect.vcxproj", "vhs.dll"),
    "heygen": cmake("build/cmake-{arch}", "Build/{platform}/Release/Heygen.dll", "Heygen.dll"),
    "nano-banana": msbuild("nano-banana.vcxproj", "NanoBanana.dll"),
    "sora2": msbuild("Sora2.vcxproj", "Sora2.dll"),
    # Only StableAudio3.dll: STABLEAUDIO3_BACKEND=NONE leaves sa3.cpp out, and
    # fetch.py builds the sa3-cpp-cpu/-gpu backends it loads into build/<arch>
    # fetch.py builds stems.exe (Demucs on ONNX Runtime) and brings
    # onnxruntime.dll - 64-bit in both packages, as Demucs does not fit in 32 bits;
    # ffmpeg decodes the input and writes the tracks
    "stem-splitter": cmake(
        "build/cmake-{arch}", "build/{arch}/StemSplitter.dll", "StemSplitter.dll",
        extras={"ffmpeg.exe": "../youtube/resources/x86/ffmpeg.exe",
                "stems.exe": "build/x64/stems.exe",
                "onnxruntime.dll": "build/x64/onnxruntime.dll"}),
    "StableAudio3": cmake(
        "build/cmake-{arch}", "build/{arch}/StableAudio3.dll", "StableAudio3.dll",
        defines={"STABLEAUDIO3_BACKEND": "NONE"},
        extras={"sa3-cpp-cpu": "build/{arch}/sa3-cpp-cpu",
                "sa3-cpp-cpu-compat": "build/{arch}/sa3-cpp-cpu-compat",
                "sa3-cpp-gpu": {"x64": "build/x64/sa3-cpp-gpu"}}),
    "sunoapi": msbuild("sunoapi.vcxproj", "SunoApi.dll"),
    "Supertonic3": msbuild(
        "Supertonic3/Supertonic3.vcxproj", "Supertonic3.dll",
        # Release links straight to build/<arch> (Link.OutputFile, MSB8012)
        output="build/{arch}/Supertonic3.dll",
        # the project copies onnxruntime.dll into build/<arch>; keep it in stage
        props={"OnnxRuntimeDestination": "{stage}/"},
        extras={"onnxruntime.dll": "Supertonic3/packages/Microsoft.ML.OnnxRuntime.1.22.1"
                                   "/runtimes/{rid}/native/onnxruntime.dll"}),
    "veo3": msbuild("Veo3.vcxproj", "Veo3.dll"),
    # The DLL runs yt-dlp.exe from its own folder; without it the plugin is a
    # dead window. Only x86 tools are kept - they run under x64 hosts too.
    "youtube": msbuild(
        "Youtube.vcxproj", "Youtube.dll",
        extras={tool: f"resources/x86/{tool}" for tool in ("yt-dlp.exe", "ffmpeg.exe", "qjs.exe")}),
}


# --- packaging ----------------------------------------------------------------


def package_entries(content: Content) -> list[tuple[Path, str]]:
    entries = []
    for name, source in content.items():
        if source.is_file():
            entries.append((source, name))
        elif source.is_dir():
            files = sorted(path for path in source.rglob("*") if path.is_file())
            if not files:
                raise BuildError(f"{source} is empty")
            entries += [(path, f"{name}/{path.relative_to(source).as_posix()}") for path in files]
        else:
            raise BuildError(f"build did not produce {source}")
    return entries


def pack(plugin: dict, arch: str, content: Content) -> Path:
    build_dir = plugin["dir"] / "build" / arch
    build_dir.mkdir(parents=True, exist_ok=True)

    # Keep the name an existing package has: two of them are force-tracked at
    # fixed paths for old installers (see CLAUDE.md). release.py renames on upload.
    existing = sorted(build_dir.glob("*.avsp"))
    target = existing[0] if len(existing) == 1 else build_dir / f"{plugin['slug']}.avsp"
    previous = set()
    for old in existing:
        with zipfile.ZipFile(old) as archive:
            previous |= {name for name in archive.namelist() if not name.endswith("/")}

    entries = package_entries(content)
    partial = target.with_suffix(".avsp.partial")
    with zipfile.ZipFile(partial, "w", zipfile.ZIP_DEFLATED) as archive:
        for source, name in entries:
            archive.write(source, name)
    for old in existing:
        old.unlink()
    partial.replace(target)

    # the checks release.py runs before it uploads anything
    if not package_is_plain_zip(target):
        raise BuildError(f"{target} is not a plain ZIP")
    if not package_has_plugin_id(target, plugin["pluginId"]):
        raise BuildError(f"no DLL in {target} contains pluginId {plugin['pluginId']!r}")

    size_mb = target.stat().st_size / (1024 * 1024)
    print(f"packed {target.relative_to(ROOT).as_posix()} ({len(entries)} files, {size_mb:.1f} MiB)")
    if previous:
        names = {name for _, name in entries}
        for sign, changed in (("+", names - previous), ("-", previous - names)):
            if changed:
                listed = ", ".join(sorted(changed)[:8]) + (" ..." if len(changed) > 8 else "")
                print(f"  {sign} {len(changed)} vs previous package: {listed}")
    return target


# --- main ---------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("plugins", nargs="*", metavar="PLUGIN",
                        help="plugin folder names (default: all)")
    parser.add_argument("--arch", choices=[*ARCHS, "both"], default="both")
    parser.add_argument("--clean", action="store_true",
                        help="rebuild everything, third-party backends (StableAudio3 CUDA) included")
    args = parser.parse_args(argv)

    folders = all_plugin_names()
    unknown = [name for name in folders if name not in PLUGINS]
    if unknown:
        print(f"error: no build recipe in build.py for: {', '.join(unknown)}", file=sys.stderr)
        return 1

    if args.plugins:
        by_lower = {name.lower(): name for name in folders}
        missing = [name for name in args.plugins if name.lower() not in by_lower]
        if missing:
            print(f"error: no such plugin folder: {', '.join(missing)}", file=sys.stderr)
            return 1
        folders = [by_lower[name.lower()] for name in args.plugins]

    archs = list(ARCHS) if args.arch == "both" else [args.arch]
    try:
        tools = find_vs2019()
    except BuildError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"Visual Studio 2019: {tools.vs_path}")

    failed: list[str] = []
    started = time.monotonic()
    for folder in folders:
        # third-party code git does not hold (NuGet, sa3.cpp and its backends)
        fetch_script = PLUGINS_DIR / folder / "fetch.py"
        if fetch_script.is_file():
            print(f"\n=== {folder} fetch ===", flush=True)
            fetch_args = ["--arch", args.arch] + (["--clean"] if args.clean else [])
            if subprocess.run([sys.executable, str(fetch_script), *fetch_args],
                              cwd=str(fetch_script.parent)).returncode != 0:
                print(f"FAILED {folder}: {fetch_script.relative_to(ROOT).as_posix()}",
                      file=sys.stderr, flush=True)
                failed += [f"{folder} {arch}" for arch in archs]
                continue
        for arch in archs:
            print(f"\n=== {folder} {arch} ===", flush=True)
            step_started = time.monotonic()
            try:
                plugin = load_plugin(folder)
                content = PLUGINS[folder](plugin["dir"], arch, tools, args.clean)
                pack(plugin, arch, content)
            except (BuildError, ReleaseError, OSError, zipfile.BadZipFile) as error:
                print(f"FAILED {folder} {arch}: {error}", file=sys.stderr, flush=True)
                failed.append(f"{folder} {arch}")
                continue
            print(f"done in {time.monotonic() - step_started:.0f}s")

    total = len(folders) * len(archs)
    print(f"\nbuilt {total - len(failed)} of {total} packages "
          f"in {(time.monotonic() - started) / 60:.1f} min")
    if failed:
        print(f"failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("next: python release.py --all --dry-run")
    return 0


if __name__ == "__main__":
    sys.exit(main())
