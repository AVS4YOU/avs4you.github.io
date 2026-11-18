"""Fetch and build the third-party backend StableAudio3 loads at runtime.

    python fetch.py                   sa3.cpp + the backends for x86 and x64
    python fetch.py --arch x64        just what the x64 package needs
    python fetch.py --clean           rebuild the backends from scratch
    python fetch.py --check           change nothing, exit 1 if anything is missing

1. sa3.cpp, with ggml as its submodule, is checked out at a pinned commit into
   sa3-cpp/ next to this file.
2. Its sa3_backend target is built through CMakePresets.json into one sa3.dll
   per backend - ggml linked in, the static runtime - in build/<arch>/:
     sa3-cpp-cpu/          x86 and x64 - AVX2
     sa3-cpp-cpu-compat/   x86 and x64 - no AVX, for CPUs without AVX2
     sa3-cpp-gpu/          x64 only - Vulkan, needs the Vulkan SDK to build
                           (VULKAN_SDK or the newest C:/VulkanSDK/<version>);
                           users need only their GPU driver
   The plugin picks among them at run time (src/system_info.cpp).

A backend is rebuilt only when what it is built from changes - the sa3.cpp
commit, CMakeLists.txt, its preset in CMakePresets.json or the Vulkan SDK - or with
--clean; otherwise this does nothing. Without a Vulkan SDK an existing GPU
backend is kept.

build.py in the repository root runs this before it builds StableAudio3.dll
itself and packs it with these backends.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

PLUGIN_DIR = Path(__file__).resolve().parent
ROOT = PLUGIN_DIR.parents[1]
sys.path.insert(0, str(ROOT))
from build import BuildError, Tools, find_vs2019, run  # noqa: E402

URL = "https://github.com/betweentwomidnights/sa3.cpp.git"
# What StableAudio3 v1.0.x was released with (branch feature/ggml-9d0d910b).
# It records ggml at 3d9d897a, the commit the released ggml-base.dll carries.
COMMIT = "378c26942e209878e2b10ac35012d0a27275870b"
SA3_CPP = PLUGIN_DIR / "sa3-cpp"

ARCHS = ("x86", "x64")


class Backend:
    """One sa3_backend build: a configure preset, its build preset and
    binaryDir from CMakePresets.json, and the folder the build fills."""

    def __init__(self, arch: str, folder: str, configure: str, build: str,
                 binary_dir: str, files: tuple[str, ...]):
        self.arch, self.configure, self.build = arch, configure, build
        self.binary_dir = PLUGIN_DIR / binary_dir
        self.output = PLUGIN_DIR / "build" / arch / folder
        # stamp next to the folder, not in it: the folder is packed whole
        self.stamp = self.output.with_name(folder + ".stamp")
        self.files = files
        self.shown = self.output.relative_to(PLUGIN_DIR).as_posix()

    @property
    def gpu(self) -> bool:
        return self.configure == "windows-vulkan"

    def complete(self) -> bool:
        return all((self.output / name).is_file() for name in self.files)


# ggml is linked into sa3.dll (CMakeLists.txt), so a backend is that one file
CPU_FILES = ("sa3.dll",)
BACKENDS = [
    Backend("x86", "sa3-cpp-cpu", "windows-x86-cpu", "x86-cpu-release", "out/x86-cpu", CPU_FILES),
    # no AVX at all: what the plugin runs on CPUs without AVX2
    Backend("x86", "sa3-cpp-cpu-compat", "windows-x86-cpu-compat", "x86-cpu-compat-release",
            "out/x86-cpu-compat", CPU_FILES),
    Backend("x64", "sa3-cpp-cpu", "windows-cpu", "cpu-release", "out/cpu", CPU_FILES),
    Backend("x64", "sa3-cpp-cpu-compat", "windows-cpu-compat", "cpu-compat-release",
            "out/cpu-compat", CPU_FILES),
    Backend("x64", "sa3-cpp-gpu", "windows-vulkan", "vulkan-release", "out/vulkan", CPU_FILES),
]


class FetchError(Exception):
    """A step failed; the message says which and why."""


# --- sa3.cpp checkout ---------------------------------------------------------


def git(*args: str, capture: bool = False, check: bool = True) -> str | None:
    """Run git in sa3-cpp/. With check=False a failure returns None."""

    if not capture:
        print(f"> git {' '.join(args)}", flush=True)
    result = subprocess.run(["git", "-C", str(SA3_CPP), *args], capture_output=capture, text=True)
    if result.returncode != 0:
        if not check:
            return None
        detail = f": {result.stderr.strip()}" if capture and result.stderr else ""
        raise FetchError(f"git {' '.join(args)} failed{detail}")
    return result.stdout.strip() if capture else ""


def checkout_in_place() -> bool:
    if not (SA3_CPP / ".git").exists():
        return False
    # no HEAD yet if an earlier run stopped before its checkout
    if git("rev-parse", "HEAD", capture=True, check=False) != COMMIT:
        return False
    # '-' not initialized, '+' at another commit, 'U' conflicted: a checkout
    # interrupted before its submodules is not done yet
    status = git("submodule", "status", "--recursive", capture=True)
    return not any(line[:1] in ("-", "+", "U") for line in status.splitlines())


def checkout() -> None:
    if (SA3_CPP / ".git").exists():
        if git("status", "--porcelain", "--untracked-files=no", capture=True):
            raise FetchError(f"{SA3_CPP} has local changes - commit, stash or delete it "
                             f"before it can be moved to {COMMIT[:10]}")
    elif SA3_CPP.exists() and any(SA3_CPP.iterdir()):
        raise FetchError(f"{SA3_CPP} exists but is not a git checkout - delete it")
    else:
        SA3_CPP.mkdir(parents=True, exist_ok=True)
        git("init", "--quiet")
        git("remote", "add", "origin", URL)

    print(f"fetching {URL} @ {COMMIT[:10]}", flush=True)
    # GitHub hands out a commit by its id, so normally only that one is
    # downloaded; failing that, take every branch and pull request
    if git("fetch", "--quiet", "--depth", "1", "origin", COMMIT, capture=True, check=False) is None:
        git("fetch", "--quiet", "origin", "+refs/heads/*:refs/remotes/origin/*",
            "+refs/pull/*/head:refs/remotes/origin/pr/*")
        if git("cat-file", "-e", f"{COMMIT}^{{commit}}", capture=True, check=False) is None:
            raise FetchError(f"commit {COMMIT} is not in {URL}")
    git("checkout", "--quiet", "--detach", COMMIT)
    git("submodule", "update", "--init", "--recursive", "--quiet")


# --- backends -------------------------------------------------------------------


def vulkan_sdk() -> Path | None:
    """The Vulkan SDK: VULKAN_SDK if it exists, else the newest C:/VulkanSDK/*
    (as ai-subtitles/fetch.py finds it)."""

    candidates = []
    if os.environ.get("VULKAN_SDK"):
        candidates.append(Path(os.environ["VULKAN_SDK"]))
    root = Path("C:/VulkanSDK")
    if root.is_dir():
        candidates += sorted((path for path in root.iterdir() if path.is_dir()),
                             key=lambda path: [int(part) if part.isdigit() else 0
                                               for part in path.name.split(".")], reverse=True)
    for sdk in candidates:
        if (sdk / "Include/vulkan/vulkan.h").is_file() and (sdk / "Bin/glslc.exe").is_file():
            return sdk
    return None


def vulkan_defines(sdk: Path) -> list[str]:
    return [
        # explicit, so a stale VULKAN_SDK cannot send CMake elsewhere
        f"-DVulkan_INCLUDE_DIR={(sdk / 'Include').as_posix()}",
        f"-DVulkan_LIBRARY={(sdk / 'Lib/vulkan-1.lib').as_posix()}",
        f"-DVulkan_GLSLC_EXECUTABLE={(sdk / 'Bin/glslc.exe').as_posix()}",
        # SPIRV-HeadersConfig.cmake, which ggml finds only through VULKAN_SDK
        f"-DCMAKE_PREFIX_PATH={sdk.as_posix()}",
    ]


def preset_chain(name: str) -> list[dict]:
    """A configure preset and every preset it inherits from."""

    presets = json.loads((PLUGIN_DIR / "CMakePresets.json").read_text(encoding="utf-8"))
    by_name = {preset["name"]: preset for preset in presets["configurePresets"]}
    chain, pending = [], [name]
    while pending:
        preset = by_name[pending.pop(0)]
        chain.append(preset)
        inherits = preset.get("inherits", [])
        pending += [inherits] if isinstance(inherits, str) else inherits
    return chain


def fingerprint(backend: Backend) -> str:
    """What the backend is built from; a change means a rebuild. Only its own
    presets count, so adding another preset rebuilds nothing."""

    digest = hashlib.sha256(COMMIT.encode())
    digest.update((PLUGIN_DIR / "CMakeLists.txt").read_bytes())
    digest.update(json.dumps(preset_chain(backend.configure), sort_keys=True).encode())
    if backend.gpu:
        sdk = vulkan_sdk()
        digest.update((sdk.name if sdk else "").encode())
    return digest.hexdigest()


def up_to_date(backend: Backend) -> bool:
    try:
        stamp = backend.stamp.read_text(encoding="ascii").strip()
    except OSError:
        return False
    return backend.complete() and stamp == fingerprint(backend)


def build_backend(backend: Backend, tools: Tools) -> None:
    # copy_if_different never deletes, so start empty or files of an older
    # build (ggml*.dll, the VC runtime, ggml-cuda.dll) would ride along; the
    # CMake cache pins the Vulkan SDK it found
    shutil.rmtree(backend.output, ignore_errors=True)
    backend.stamp.unlink(missing_ok=True)
    shutil.rmtree(backend.binary_dir, ignore_errors=True)

    configure = [tools.cmake, "--preset", backend.configure,
                 f"-DCMAKE_GENERATOR_INSTANCE={tools.vs_path}"]
    if backend.gpu:
        configure += vulkan_defines(vulkan_sdk())
    run(configure, cwd=PLUGIN_DIR)
    # only the backend: StableAudio3.dll itself is build.py's job
    run([tools.cmake, "--build", "--preset", backend.build, "--target", "sa3_backend"],
        cwd=PLUGIN_DIR)

    if not backend.complete():
        raise FetchError(f"the build did not fill {backend.shown}")
    backend.stamp.write_text(fingerprint(backend), encoding="ascii")


def keep_gpu_backend(backend: Backend) -> None:
    """No Vulkan SDK here: keep the Vulkan backend there is rather than ship a
    package without GPU support. A CUDA one from before 1.1 does not count."""

    if backend.complete() and not (backend.output / "ggml-cuda.dll").exists():
        print(f"WARNING: no Vulkan SDK - keeping {backend.shown} as it is", flush=True)
        return
    raise FetchError(f"{backend.shown} needs the Vulkan SDK (https://vulkan.lunarg.com): "
                     f"its headers, vulkan-1.lib and glslc")


# --- main -------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--arch", choices=[*ARCHS, "both"], default="both")
    parser.add_argument("--clean", action="store_true",
                        help="rebuild the backends from scratch, Vulkan included")
    parser.add_argument("--check", action="store_true",
                        help="change nothing; exit 1 if anything is missing or out of date")
    args = parser.parse_args(argv)

    archs = ARCHS if args.arch == "both" else (args.arch,)
    backends = [backend for backend in BACKENDS if backend.arch in archs]

    try:
        if checkout_in_place():
            print(f"ok:      sa3-cpp @ {COMMIT[:10]}")
        elif args.check:
            print(f"missing: sa3-cpp @ {COMMIT[:10]}")
            return 1
        else:
            checkout()
            print(f"ok:      sa3-cpp @ {COMMIT[:10]}")

        if args.check:
            stale = [backend.shown for backend in backends if not up_to_date(backend)]
            for backend in backends:
                print(f"{'missing:' if backend.shown in stale else 'ok:     '} {backend.shown}")
            return 1 if stale else 0

        tools = None
        for backend in backends:
            if up_to_date(backend) and not args.clean:
                print(f"ok:      {backend.shown}")
                continue
            if backend.gpu and not vulkan_sdk():
                keep_gpu_backend(backend)
                continue
            print(f"building {backend.shown}" + (" - Vulkan, the shaders take a while"
                                                 if backend.gpu else ""), flush=True)
            tools = tools or find_vs2019()
            build_backend(backend, tools)
            print(f"ok:      {backend.shown}")
    except (FetchError, BuildError, OSError, zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
