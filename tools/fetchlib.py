"""Helpers the plugins' fetch.py scripts share: third-party sources at a pinned
commit, CMake builds of them, and stamps that make a rebuild happen only when
what a build is made from changes.

A plugins/<folder>/fetch.py imports this as

    sys.path.insert(0, str(ROOT / "tools"))
    import fetchlib

Tool discovery (Visual Studio 2019, CMake) and run() come from the root
build.py, so a fetch.py and build.py always pick the same toolchain.
"""

from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from build import BuildError, Tools, find_vs2019, run  # noqa: E402,F401

PLATFORM = {"x86": "Win32", "x64": "x64"}

# ggml's instruction sets. Its default with GGML_NATIVE off is AVX2/FMA/F16C;
# COMPAT turns all of them off for CPUs without AVX2 - the plugin picks the
# build at run time.
GGML_COMPAT = {name: "OFF" for name in (
    "GGML_SSE42", "GGML_AVX", "GGML_AVX2", "GGML_AVX_VNNI", "GGML_FMA", "GGML_F16C",
    "GGML_BMI2", "GGML_AVX512")}


def ggml_defines(arch: str, compat: bool) -> dict[str, str]:
    """What every ggml build of ours needs: no build-machine CPU tuning, and
    on 32-bit no BMI2 (MSVC has no _pdep_u64 there)."""

    defines = {"GGML_NATIVE": "OFF"}
    if arch == "x86":
        defines["GGML_BMI2"] = "OFF"
    if compat:
        defines.update(GGML_COMPAT)
    return defines


class FetchError(Exception):
    """A step failed; the message says which and why."""


# --- sources at a pinned commit -------------------------------------------------


def git(repo: Path, *args: str, capture: bool = False, check: bool = True) -> str | None:
    """Run git in repo. With check=False a failure returns None."""

    if not capture:
        print(f"> git {' '.join(args)}", flush=True)
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=capture, text=True)
    if result.returncode != 0:
        if not check:
            return None
        detail = f": {result.stderr.strip()}" if capture and result.stderr else ""
        raise FetchError(f"git {' '.join(args)} failed in {repo}{detail}")
    return result.stdout.strip() if capture else ""


def checkout_in_place(target: Path, commit: str, submodules: list[str] | None = None) -> bool:
    if not (target / ".git").exists():
        return False
    # no HEAD yet if an earlier run stopped before its checkout
    if git(target, "rev-parse", "HEAD", capture=True, check=False) != commit:
        return False
    # '-' not initialized, '+' at another commit, 'U' conflicted: a checkout
    # interrupted before its submodules is not done yet
    status = git(target, "submodule", "status", "--recursive", *(submodules or []), capture=True)
    return not any(line[:1] in ("-", "+", "U") for line in status.splitlines())


def checkout(target: Path, url: str, commit: str, submodules: list[str] | None = None) -> None:
    """Make target a checkout of url at commit, with its submodules - or only
    the listed ones, when the rest are huge or unreachable. A checkout with
    local changes is never moved."""

    if (target / ".git").exists():
        if git(target, "status", "--porcelain", "--untracked-files=no", capture=True):
            raise FetchError(f"{target} has local changes - commit, stash or delete it "
                             f"before it can be moved to {commit[:10]}")
    elif target.exists() and any(target.iterdir()):
        raise FetchError(f"{target} exists but is not a git checkout - delete it")
    else:
        target.mkdir(parents=True, exist_ok=True)
        git(target, "init", "--quiet")
        git(target, "remote", "add", "origin", url)

    print(f"fetching {url} @ {commit[:10]}", flush=True)
    # GitHub hands out a commit by its id, so normally only that one is
    # downloaded; failing that, take every branch and pull request
    if git(target, "fetch", "--quiet", "--depth", "1", "origin", commit,
           capture=True, check=False) is None:
        git(target, "fetch", "--quiet", "origin", "+refs/heads/*:refs/remotes/origin/*",
            "+refs/pull/*/head:refs/remotes/origin/pr/*")
        if git(target, "cat-file", "-e", f"{commit}^{{commit}}", capture=True, check=False) is None:
            raise FetchError(f"commit {commit} is not in {url}")
    git(target, "checkout", "--quiet", "--detach", commit)
    git(target, "submodule", "update", "--init", "--recursive", "--quiet", *(submodules or []))


def ensure_checkout(target: Path, url: str, commit: str, check: bool,
                    submodules: list[str] | None = None) -> bool:
    """True if target is (now) at commit; with check, report without fetching."""

    shown = f"{target.name} @ {commit[:10]}"
    if checkout_in_place(target, commit, submodules):
        print(f"ok:      {shown}")
        return True
    if check:
        print(f"missing: {shown}")
        return False
    checkout(target, url, commit, submodules)
    print(f"ok:      {shown}")
    return True


# --- downloads ---------------------------------------------------------------------


def download(url: str, target: Path, size: int | None = None) -> None:
    """Download url to target via a .partial file, so an interrupted download
    is never mistaken for a complete one."""

    target.parent.mkdir(parents=True, exist_ok=True)
    print(f"downloading {url}", flush=True)
    with tempfile.NamedTemporaryFile(dir=target.parent, suffix=".partial", delete=False) as handle:
        partial = Path(handle.name)
    try:
        urllib.request.urlretrieve(url, partial)
        if size is not None and partial.stat().st_size != size:
            raise FetchError(f"{url}: got {partial.stat().st_size} bytes, expected {size}")
        partial.replace(target)
    finally:
        partial.unlink(missing_ok=True)


def ensure_nuget(package_id: str, version: str, packages_dir: Path, check: bool) -> bool:
    """What 'nuget restore' does for one package, without nuget.exe: it is
    unpacked into packages_dir/<id>.<version>. True if it is (now) there."""

    target = packages_dir / f"{package_id}.{version}"
    shown = f"{package_id} {version}"
    if target.is_dir():
        print(f"ok:      {shown}")
        return True
    if check:
        print(f"missing: {shown}")
        return False
    packages_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=packages_dir) as scratch:
        nupkg = Path(scratch) / "package.nupkg"
        download(f"https://www.nuget.org/api/v2/package/{package_id}/{version}", nupkg)
        unpacked = Path(scratch) / "unpacked"
        with zipfile.ZipFile(nupkg) as archive:
            archive.extractall(unpacked)
        # renamed only once complete, so an interrupted fetch is retried
        unpacked.rename(target)
    print(f"ok:      {shown}")
    return True


# --- builds and their stamps ---------------------------------------------------


def fingerprint(*parts: str | bytes | Path) -> str:
    """A digest of what a build is made from: strings, bytes, or files."""

    digest = hashlib.sha256()
    for part in parts:
        if isinstance(part, Path):
            part = part.read_bytes()
        digest.update(part.encode() if isinstance(part, str) else part)
        digest.update(b"\0")
    return digest.hexdigest()


def up_to_date(stamp: Path, outputs: list[Path], expected: str) -> bool:
    try:
        recorded = stamp.read_text(encoding="ascii").strip()
    except OSError:
        return False
    return recorded == expected and all(output.is_file() for output in outputs)


def cmake_build(tools: Tools, source: Path, binary_dir: Path, arch: str,
                defines: dict[str, str], targets: list[str], fresh: bool = True,
                env: dict[str, str] | None = None) -> None:
    """Configure source into binary_dir for VS2019 Win32/x64 and build targets
    in Release. fresh starts from an empty binary_dir, so no cached option
    survives a change of defines."""

    if fresh:
        shutil.rmtree(binary_dir, ignore_errors=True)
    run([tools.cmake, "-S", str(source), "-B", str(binary_dir),
         "-G", "Visual Studio 16 2019", "-A", PLATFORM[arch],
         f"-DCMAKE_GENERATOR_INSTANCE={tools.vs_path}",
         *[f"-D{key}={value}" for key, value in defines.items()]])
    run([tools.cmake, "--build", str(binary_dir), "--config", "Release",
         *[arg for target in targets for arg in ("--target", target)], "--", "/m"], env=env)
