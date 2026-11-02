"""Fetch and build the third-party backend StableAudio3 loads at runtime.

    python fetch.py                   sa3.cpp + the backends for x86 and x64
    python fetch.py --arch x64        just what the x64 package needs
    python fetch.py --clean           rebuild the backends from scratch
    python fetch.py --check           change nothing, exit 1 if anything is missing

1. sa3.cpp, with ggml as its submodule, is checked out at a pinned commit into
   sa3-cpp/ next to this file.
2. Its sa3_backend target is built through CMakePresets.json, which copies
   sa3.dll, ggml*.dll and the VC/UCRT runtime into build/<arch>/:
     sa3-cpp-cpu/          x86 and x64 - AVX2
     sa3-cpp-cpu-compat/   x86 and x64 - no AVX, for CPUs without AVX2
     sa3-cpp-gpu/          x64 only - CUDA, needs the CUDA Toolkit (12.8+ keeps it small)
   The plugin picks among them at run time (src/system_info.cpp).

A backend is rebuilt only when what it is built from changes - the sa3.cpp
commit, CMakeLists.txt, its preset in CMakePresets.json or the CUDA version - or with
--clean; otherwise this does nothing. Without a CUDA Toolkit an existing GPU
backend is kept, or taken from the previous x64 package in build/x64.

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
        return self.configure == "windows-cuda"

    def complete(self) -> bool:
        return all((self.output / name).is_file() for name in self.files)


CPU_FILES = ("sa3.dll", "ggml.dll", "ggml-base.dll", "ggml-cpu.dll")
BACKENDS = [
    Backend("x86", "sa3-cpp-cpu", "windows-x86-cpu", "x86-cpu-release", "out/x86-cpu", CPU_FILES),
    # no AVX at all: what the plugin runs on CPUs without AVX2
    Backend("x86", "sa3-cpp-cpu-compat", "windows-x86-cpu-compat", "x86-cpu-compat-release",
            "out/x86-cpu-compat", CPU_FILES),
    Backend("x64", "sa3-cpp-cpu", "windows-cpu", "cpu-release", "out/cpu", CPU_FILES),
    Backend("x64", "sa3-cpp-cpu-compat", "windows-cpu-compat", "cpu-compat-release",
            "out/cpu-compat", CPU_FILES),
    Backend("x64", "sa3-cpp-gpu", "windows-cuda", "cuda-release", "out/cuda",
            CPU_FILES + ("ggml-cuda.dll",)),
]

# ggml compresses its CUDA kernels (-compress-mode=size) only from this
# version on; older toolkits make ggml-cuda.dll about four times larger
CUDA_COMPRESSION = (12, 8)


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


def cuda_toolkit() -> Path | None:
    """The CUDA Toolkit CUDA_PATH names - the installer points it at the newest."""

    cuda_path = os.environ.get("CUDA_PATH")
    if cuda_path and (Path(cuda_path) / "bin" / "nvcc.exe").exists():
        return Path(cuda_path)
    return None


def cuda_version(toolkit: Path) -> tuple[int, ...]:
    try:
        version = json.loads((toolkit / "version.json").read_text(encoding="utf-8"))
        return tuple(int(part) for part in version["cuda"]["version"].split("."))
    except (OSError, ValueError, KeyError):
        return ()


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
        toolkit = cuda_toolkit()
        digest.update(".".join(map(str, cuda_version(toolkit) if toolkit else ())).encode())
    return digest.hexdigest()


def up_to_date(backend: Backend) -> bool:
    try:
        stamp = backend.stamp.read_text(encoding="ascii").strip()
    except OSError:
        return False
    return backend.complete() and stamp == fingerprint(backend)


def build_backend(backend: Backend, tools: Tools) -> None:
    # copy_if_different never deletes, so start empty or files of an older
    # sa3.cpp would ride along; the CMake cache pins the CUDA Toolkit it found
    shutil.rmtree(backend.output, ignore_errors=True)
    backend.stamp.unlink(missing_ok=True)
    shutil.rmtree(backend.binary_dir, ignore_errors=True)

    configure = [tools.cmake, "--preset", backend.configure,
                 f"-DCMAKE_GENERATOR_INSTANCE={tools.vs_path}"]
    env = None
    if backend.gpu:
        toolkit = cuda_toolkit()
        version = cuda_version(toolkit)
        if version and version < CUDA_COMPRESSION:
            print(f"WARNING: CUDA {'.'.join(map(str, version))} cannot compress the kernels - "
                  f"ggml-cuda.dll comes out about four times larger than with CUDA 12.8+",
                  flush=True)
        # with several toolkits installed, build with the CUDA_PATH one
        configure += ["-T", f"cuda={toolkit}"]
        # MSBuild compiles the .cu files one at a time; let nvcc at least
        # build each for all its architectures at once
        env = {"NVCC_APPEND_FLAGS":
               f"{os.environ.get('NVCC_APPEND_FLAGS', '')} --threads 0".strip()}
    run(configure, cwd=PLUGIN_DIR)
    # only the backend: StableAudio3.dll itself is build.py's job
    run([tools.cmake, "--build", "--preset", backend.build, "--target", "sa3_backend"],
        cwd=PLUGIN_DIR, env=env)

    if not backend.complete():
        raise FetchError(f"the build did not fill {backend.shown}")
    backend.stamp.write_text(fingerprint(backend), encoding="ascii")


def keep_gpu_backend(backend: Backend) -> None:
    """No CUDA Toolkit here: keep the GPU backend there is, or the one in the
    previous x64 package, rather than ship a package without GPU support."""

    if backend.complete():
        print(f"WARNING: no CUDA Toolkit - keeping {backend.shown} as it is", flush=True)
        return

    for previous in sorted(backend.output.parent.glob("*.avsp")):
        with zipfile.ZipFile(previous) as archive:
            prefix = backend.output.name + "/"
            members = [name for name in archive.namelist()
                       if name.startswith(prefix) and not name.endswith("/")]
            if not members:
                continue
            print(f"WARNING: no CUDA Toolkit - taking {prefix} ({len(members)} files) "
                  f"from the previous {previous.name}", flush=True)
            shutil.rmtree(backend.output, ignore_errors=True)
            archive.extractall(backend.output.parent, members)
            return

    raise FetchError(f"{backend.shown} needs the CUDA Toolkit, or a previous x64 package "
                     f"with it in build/x64")


# --- main -------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--arch", choices=[*ARCHS, "both"], default="both")
    parser.add_argument("--clean", action="store_true",
                        help="rebuild the backends from scratch, CUDA included")
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
            if backend.gpu and not cuda_toolkit():
                keep_gpu_backend(backend)
                continue
            print(f"building {backend.shown}" + (" - CUDA, this takes a while" if backend.gpu
                                                 else ""), flush=True)
            tools = tools or find_vs2019()
            build_backend(backend, tools)
            print(f"ok:      {backend.shown}")
    except (FetchError, BuildError, OSError, zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
