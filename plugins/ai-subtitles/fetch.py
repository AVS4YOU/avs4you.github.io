"""Fetch and build whisper.cpp, the speech recognizer AI Subtitles drives.

    python fetch.py                   whisper.cpp + whisper-cli for x86 and x64
    python fetch.py --arch x64        just what the x64 package needs
    python fetch.py --clean           rebuild from scratch
    python fetch.py --check           change nothing, exit 1 if anything is missing

1. whisper.cpp is checked out at a pinned release into whisper-cpp/.
2. whisper-cli is built per architecture into build/<arch>/:
     whisper-cli.exe          AVX2/FMA/F16C
     whisper-cli-compat.exe   no AVX, for CPUs without AVX2
     whisper-cli-vulkan.exe   x64 only: AVX2 plus ggml's Vulkan backend
   All are single static executables (/MT, no OpenMP); the plugin picks one
   at run time. The Vulkan one also needs vulkan-1.dll, which every GPU driver
   with Vulkan installs; without a GPU it runs on the CPU as fast as
   whisper-cli.exe, and without vulkan-1.dll it does not start - the plugin
   then runs whisper-cli.exe. The x86 ones are large address aware, so the
   large model fits on 64-bit Windows.

The Vulkan build needs the Vulkan SDK (https://vulkan.lunarg.com - headers,
vulkan-1.lib, glslc for the shaders), found through VULKAN_SDK or the newest
C:/VulkanSDK/<version>.

A build is redone only when the commit or its CMake options change, or with
--clean. build.py in the repository root runs this first. The Whisper models
are not fetched here: the plugin downloads the one the user picks.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

PLUGIN_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(PLUGIN_DIR.parents[1] / "tools"))
import fetchlib  # noqa: E402

URL = "https://github.com/ggml-org/whisper.cpp.git"
COMMIT = "d1be6fde11ac6e0407606b4e42fe72d34add8037"  # v1.9.5
SOURCE = PLUGIN_DIR / "whisper-cpp"

ARCHS = ("x86", "x64")
# name: (compat, vulkan, architectures)
VARIANTS = {
    "whisper-cli.exe": (False, False, ARCHS),
    "whisper-cli-compat.exe": (True, False, ARCHS),
    "whisper-cli-vulkan.exe": (False, True, ("x64",)),
}


def vulkan_sdk() -> Path:
    """The Vulkan SDK: VULKAN_SDK if it exists, else the newest C:/VulkanSDK/*."""

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
    raise fetchlib.FetchError("the Vulkan SDK is not installed (https://vulkan.lunarg.com); "
                              "whisper-cli-vulkan.exe needs its headers, vulkan-1.lib and glslc")


def defines(arch: str, compat: bool, vulkan: bool = False) -> dict[str, str]:
    values = {
        "BUILD_SHARED_LIBS": "OFF",
        # vcomp140.dll has no static form; ggml's own thread pool needs nothing
        "GGML_OPENMP": "OFF",
        "WHISPER_BUILD_TESTS": "OFF",
        "WHISPER_BUILD_SERVER": "OFF",
        "WHISPER_BUILD_EXAMPLES": "ON",
        # the static CRT, so no VC++ redistributable is needed
        "CMAKE_POLICY_DEFAULT_CMP0091": "NEW",
        "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreaded",
    }
    if arch == "x86":
        values["CMAKE_EXE_LINKER_FLAGS"] = "/LARGEADDRESSAWARE"
    values.update(fetchlib.ggml_defines(arch, compat))
    if vulkan:
        sdk = vulkan_sdk()
        values.update({
            "GGML_VULKAN": "ON",
            # explicit, so a stale VULKAN_SDK cannot send CMake elsewhere
            "Vulkan_INCLUDE_DIR": (sdk / "Include").as_posix(),
            "Vulkan_LIBRARY": (sdk / "Lib/vulkan-1.lib").as_posix(),
            "Vulkan_GLSLC_EXECUTABLE": (sdk / "Bin/glslc.exe").as_posix(),
            # SPIRV-HeadersConfig.cmake, which ggml finds only through VULKAN_SDK
            "CMAKE_PREFIX_PATH": sdk.as_posix(),
        })
    return values


def build(arch: str, name: str, compat: bool, vulkan: bool, clean: bool, check: bool,
          tools: list) -> bool:
    output = PLUGIN_DIR / "build" / arch / name
    stamp = output.with_suffix(".stamp")
    shown = output.relative_to(PLUGIN_DIR).as_posix()
    values = defines(arch, compat, vulkan)
    expected = fetchlib.fingerprint(COMMIT, json.dumps(values, sort_keys=True))

    if not clean and fetchlib.up_to_date(stamp, [output], expected):
        print(f"ok:      {shown}")
        return True
    if check:
        print(f"missing: {shown}")
        return False

    print(f"building {shown}", flush=True)
    if not tools:
        tools.append(fetchlib.find_vs2019())
    binary_dir = PLUGIN_DIR / "out" / f"{arch}{'-compat' if compat else ''}{'-vulkan' if vulkan else ''}"
    fetchlib.cmake_build(tools[0], SOURCE, binary_dir, arch, values, ["whisper-cli"])
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary_dir / "bin" / "Release" / "whisper-cli.exe", output)
    stamp.write_text(expected, encoding="ascii")
    print(f"ok:      {shown}")
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--arch", choices=[*ARCHS, "both"], default="both")
    parser.add_argument("--clean", action="store_true", help="rebuild from scratch")
    parser.add_argument("--check", action="store_true",
                        help="change nothing; exit 1 if anything is missing or out of date")
    args = parser.parse_args(argv)
    archs = ARCHS if args.arch == "both" else (args.arch,)

    tools: list = []
    in_place = True
    try:
        in_place &= fetchlib.ensure_checkout(SOURCE, URL, COMMIT, args.check)
        for arch in archs:
            for name, (compat, vulkan, variant_archs) in VARIANTS.items():
                if arch in variant_archs:
                    in_place &= build(arch, name, compat, vulkan, args.clean, args.check, tools)
    except (fetchlib.FetchError, fetchlib.BuildError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0 if in_place or not args.check else 1


if __name__ == "__main__":
    sys.exit(main())
