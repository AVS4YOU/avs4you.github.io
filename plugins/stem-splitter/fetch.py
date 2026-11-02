"""Fetch and build the separation engine Stem Splitter drives.

    python fetch.py                   demucs.onnx + ONNX Runtime + stems.exe for x86 and x64
    python fetch.py --arch x64        just what the x64 package needs
    python fetch.py --clean           rebuild from scratch
    python fetch.py --check           change nothing, exit 1 if anything is missing

1. demucs.onnx (STFT and model glue for Demucs v4 as ONNX) is checked out at
   a pinned commit into demucs-onnx/, with only its Eigen submodule.
2. ONNX Runtime comes from its official NuGet package, into packages/.
3. engine/ - our front end and segment cross-fade - is built against both
   into build/x64/stems.exe, with ONNX Runtime's onnxruntime.dll beside it.
   ONNX Runtime picks its CPU kernels at run time, so one build serves every CPU.

There is no 32-bit engine: Demucs's attention needs blocks of a few hundred MB
that a 32-bit address space cannot give, so the x86 package carries the x64
engine as well and the plugin runs it on 64-bit Windows. --arch is accepted,
like every fetch.py, and the x64 engine is built for either.

A build is redone only when the commit, the ONNX Runtime version or engine/
changes, or with --clean. build.py in the repository root runs this first.
The Demucs ONNX models are not fetched here: the plugin downloads the one the
user picks (see tools/export_demucs_onnx.py for how they are made).
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

PLUGIN_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(PLUGIN_DIR.parents[1] / "tools"))
import fetchlib  # noqa: E402

URL = "https://github.com/sevagh/demucs.onnx.git"
COMMIT = "81fa192e6fcc88e35e887f6e6ccce91227f4e6f5"
SOURCE = PLUGIN_DIR / "demucs-onnx"
SUBMODULES = ["vendor/eigen"]  # its others: ONNX Runtime's sources, and ssh-only ones

ORT_ID, ORT_VERSION = "Microsoft.ML.OnnxRuntime", "1.22.1"
PACKAGES = PLUGIN_DIR / "packages"
ORT_DIR = PACKAGES / f"{ORT_ID}.{ORT_VERSION}"
NUGET_RID = {"x86": "win-x86", "x64": "win-x64"}

ENGINE = PLUGIN_DIR / "engine"
ARCHS = ("x86", "x64")
ENGINE_ARCH = "x64"


def build(arch: str, clean: bool, check: bool, tools: list) -> bool:
    output_dir = PLUGIN_DIR / "build" / arch
    outputs = [output_dir / "stems.exe", output_dir / "onnxruntime.dll"]
    stamp = output_dir / "stems.stamp"
    shown = f"build/{arch}/stems.exe"
    engine_files = sorted(path for path in ENGINE.iterdir() if path.is_file())
    expected = fetchlib.fingerprint(COMMIT, ORT_VERSION, *[path.name for path in engine_files],
                                    *engine_files)

    if not clean and fetchlib.up_to_date(stamp, outputs, expected):
        print(f"ok:      {shown}")
        return True
    if check:
        print(f"missing: {shown}")
        return False

    print(f"building {shown}", flush=True)
    if not tools:
        tools.append(fetchlib.find_vs2019())
    binary_dir = PLUGIN_DIR / "out" / arch
    fetchlib.cmake_build(tools[0], ENGINE, binary_dir, arch,
                         {"DEMUCS_ONNX_DIR": SOURCE.as_posix(), "ORT_DIR": ORT_DIR.as_posix()},
                         ["stems"])
    output_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary_dir / "Release" / "stems.exe", outputs[0])
    shutil.copy2(ORT_DIR / "runtimes" / NUGET_RID[arch] / "native" / "onnxruntime.dll", outputs[1])
    stamp.write_text(expected, encoding="ascii")
    print(f"ok:      {shown}")
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--arch", choices=[*ARCHS, "both"], default="both",
                        help="accepted like every fetch.py; the engine is x64 for both")
    parser.add_argument("--clean", action="store_true", help="rebuild from scratch")
    parser.add_argument("--check", action="store_true",
                        help="change nothing; exit 1 if anything is missing or out of date")
    args = parser.parse_args(argv)
    tools: list = []
    in_place = True
    try:
        in_place &= fetchlib.ensure_checkout(SOURCE, URL, COMMIT, args.check, SUBMODULES)
        in_place &= fetchlib.ensure_nuget(ORT_ID, ORT_VERSION, PACKAGES, args.check)
        in_place &= build(ENGINE_ARCH, args.clean, args.check, tools)
    except (fetchlib.FetchError, fetchlib.BuildError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0 if in_place or not args.check else 1


if __name__ == "__main__":
    sys.exit(main())
