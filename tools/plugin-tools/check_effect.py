#!/usr/bin/env python3
"""Load an effect plugin DLL and check the things a preview GIF cannot show.

    python tools/plugin-tools/check_effect.py effect-snow

Checks
  exports        every symbol in module.def resolves and the info calls answer
  determinism    the same frame index always renders the same pixels
  motion         where the frame changes over time, by how much, and in
                 which direction (phase correlation between frames)
  coherence      neighbouring frames must correlate far better than distant
                 ones - if they do not, the effect is re-randomising per frame
                 instead of animating
  performance    milliseconds per 1920x1080 frame

The DLL is loaded in-process, so its architecture has to match the running
Python. A 64-bit Python needs the x64 build (the default).
"""

from __future__ import annotations

import argparse
import ctypes
import json
import struct
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
PLUGINS_DIR = ROOT / "plugins"

REQUIRED_EXPORTS = [
    "PluginType", "PluginId", "PluginName", "PluginVersion", "PluginIcon",
    "IsApplicationSupported", "ReleasePluginString",
    "GetEffectsCount", "GetEffectName", "GetEffectParams", "ApplyEffect",
]

PLUGIN_TYPE_NAMES = {0: "Unknown", 1: "Content", 2: "ImageEffect"}

APP_IDS = {
    "Video Converter": 21, "Video Remaker": 22, "Video Editor": 4,
    "Photo Editor": 5, "Image Converter": 51,
    "Audio Editor": 1, "Audio Converter": 35,
}

problems: list[str] = []
warnings: list[str] = []


def ok(message: str) -> None:
    print(f"  [ok]   {message}")


def warn(message: str) -> None:
    warnings.append(message)
    print(f"  [warn] {message}")


def bad(message: str) -> None:
    problems.append(message)
    print(f"  [FAIL] {message}")


def find_dll(plugin_dir: Path, arch: str) -> Path:
    out_dir = plugin_dir / ("x64/Release" if arch == "x64" else "Release")
    dlls = sorted(out_dir.glob("*.dll"))
    if not dlls:
        raise SystemExit(f"ERROR: no DLL in {out_dir}. Build it first:\n"
                         f"  python tools/plugin-tools/build_effect.py {plugin_dir.name}")
    return dlls[0]


def make_test_frame(w: int, h: int) -> np.ndarray:
    """Gradient plus bright and dark patches, so effects of any polarity show."""
    frame = np.zeros((h, w, 4), dtype=np.uint8)
    xs = np.linspace(20, 235, w, dtype=np.float32)[None, :]
    ys = np.linspace(30, 200, h, dtype=np.float32)[:, None]
    frame[:, :, 0] = np.clip(xs * 0.6 + ys * 0.4, 0, 255).astype(np.uint8)
    frame[:, :, 1] = np.clip(ys, 0, 255).astype(np.uint8)
    frame[:, :, 2] = np.clip(xs, 0, 255).astype(np.uint8)
    frame[:, :, 3] = 255
    frame[h // 8: h // 4, w // 8: w // 3] = (250, 250, 250, 255)
    frame[h * 3 // 4: h * 7 // 8, w * 2 // 3: w * 8 // 9] = (6, 6, 6, 255)
    return frame


def phase_shift(a: np.ndarray, b: np.ndarray) -> tuple[int, int, float]:
    """Dominant (dy, dx) translation from a to b, plus its correlation."""
    a = a - a.mean()
    b = b - b.mean()
    denom = float(np.sqrt((a * a).sum() * (b * b).sum())) + 1e-9

    spectrum = np.fft.rfft2(a).conj() * np.fft.rfft2(b)
    cc = np.fft.irfft2(spectrum, s=a.shape)

    dy, dx = np.unravel_index(int(np.argmax(cc)), cc.shape)
    peak = float(cc[dy, dx]) / denom

    h, w = a.shape
    if dy > h // 2:
        dy -= h
    if dx > w // 2:
        dx -= w
    return int(dy), int(dx), peak


class Effect:
    def __init__(self, dll_path: Path):
        self.dll = ctypes.CDLL(str(dll_path))

        self.apply = self.dll.ApplyEffect
        self.apply.restype = ctypes.c_int
        self.apply.argtypes = [ctypes.POINTER(ctypes.c_ubyte), ctypes.c_int, ctypes.c_int,
                               ctypes.c_double, ctypes.c_int, ctypes.c_void_p,
                               ctypes.POINTER(ctypes.c_void_p)]

        self.release_string = getattr(self.dll, "ReleasePluginString", None)
        if self.release_string:
            self.release_string.restype = None
            self.release_string.argtypes = [ctypes.c_void_p]

        self.release_data = getattr(self.dll, "ReleaseEffectData", None)
        if self.release_data:
            self.release_data.restype = None
            self.release_data.argtypes = [ctypes.c_void_p]

    def wide_string(self, name: str) -> str | None:
        fn = getattr(self.dll, name, None)
        if fn is None:
            return None
        fn.restype = ctypes.c_void_p
        fn.argtypes = []
        ptr = fn()
        if not ptr:
            return None
        value = ctypes.wstring_at(ptr)
        if self.release_string:
            self.release_string(ptr)
        return value

    def wide_string_at(self, name: str, index: int) -> str | None:
        fn = getattr(self.dll, name, None)
        if fn is None:
            return None
        fn.restype = ctypes.c_void_p
        fn.argtypes = [ctypes.c_int]
        ptr = fn(index)
        if not ptr:
            return None
        value = ctypes.wstring_at(ptr)
        if self.release_string:
            self.release_string(ptr)
        return value

    def int_call(self, name: str, *args) -> int | None:
        fn = getattr(self.dll, name, None)
        if fn is None:
            return None
        fn.restype = ctypes.c_int
        fn.argtypes = [ctypes.c_int] * len(args)
        return int(fn(*args))

    def render(self, source: np.ndarray, completeness: float, state: ctypes.c_void_p) -> np.ndarray:
        h, w = source.shape[:2]
        buf = (ctypes.c_ubyte * (w * h * 4)).from_buffer_copy(source.tobytes())
        hr = self.apply(buf, w, h, ctypes.c_double(completeness), 0, None, ctypes.byref(state))
        if hr != 0:
            raise SystemExit(f"ERROR: ApplyEffect returned 0x{hr & 0xffffffff:08x}")
        return np.frombuffer(bytes(buf), dtype=np.uint8).reshape(h, w, 4)

    def free_state(self, state: ctypes.c_void_p) -> None:
        if state and state.value and self.release_data:
            self.release_data(state)
        state.value = None


def sequence(effect: Effect, source: np.ndarray, frames: int) -> list[np.ndarray]:
    state = ctypes.c_void_p(None)
    try:
        return [effect.render(source, i / max(1, frames - 1), state) for i in range(frames)]
    finally:
        effect.free_state(state)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("plugin")
    parser.add_argument("--arch", choices=["x86", "x64"],
                        default="x64" if struct.calcsize("P") == 8 else "x86")
    parser.add_argument("--size", default="320x180", help="test frame size, WxH")
    parser.add_argument("--frames", type=int, default=40)
    parser.add_argument("--perf-size", default="1920x1080")
    args = parser.parse_args()

    python_arch = "x64" if struct.calcsize("P") == 8 else "x86"
    if args.arch != python_arch:
        raise SystemExit(f"ERROR: this Python is {python_arch}; it can only load "
                         f"{python_arch} DLLs. Re-run with --arch {python_arch}.")

    plugin_dir = (PLUGINS_DIR / args.plugin).resolve()
    if not plugin_dir.is_dir():
        raise SystemExit(f"ERROR: no such plugin folder: {plugin_dir}")

    dll_path = find_dll(plugin_dir, args.arch)
    print(f"plugin : {plugin_dir.name}")
    print(f"dll    : {dll_path.relative_to(ROOT).as_posix()}\n")

    effect = Effect(dll_path)

    # ---- exports ---------------------------------------------------------
    print("exports")
    def_file = plugin_dir / "module.def"
    expected = list(REQUIRED_EXPORTS)
    if def_file.exists():
        for line in def_file.read_text(encoding="utf-8-sig").splitlines():
            name = line.strip()
            if name and name.upper() != "EXPORTS" and name not in expected:
                expected.append(name)

    for name in expected:
        if getattr(effect.dll, name, None) is None:
            bad(f"{name} is not exported")
    if not problems:
        ok(f"all {len(expected)} exports resolve")

    plugin_type = effect.int_call("PluginType")
    if plugin_type != 2:
        bad(f"PluginType() returned {PLUGIN_TYPE_NAMES.get(plugin_type, plugin_type)}, "
            f"expected ImageEffect")
    else:
        ok("PluginType() = ImageEffect")

    for name in ("PluginId", "PluginName", "PluginVersion"):
        value = effect.wide_string(name)
        if not value:
            bad(f"{name}() returned an empty string")
        else:
            ok(f"{name}() = {value!r}")

    count = effect.int_call("GetEffectsCount")
    if not count or count < 1:
        bad(f"GetEffectsCount() = {count}")
    else:
        names = [effect.wide_string_at("GetEffectName", i) for i in range(count)]
        ok(f"GetEffectsCount() = {count}, names = {names}")

    config_path = plugin_dir / "config.json"
    if config_path.exists():
        config = json.loads(config_path.read_text(encoding="utf-8"))
        for app in config.get("apps", []):
            app_id = APP_IDS.get(app)
            if app_id is None:
                warn(f"config.json lists app {app!r}, which has no known id")
                continue
            if not effect.int_call("IsApplicationSupported", app_id):
                bad(f"config.json advertises {app!r} but IsApplicationSupported({app_id}) is false")
        if not any(p.startswith("config.json advertises") for p in problems):
            ok(f"IsApplicationSupported agrees with config.json {config.get('apps')}")
    else:
        warn("config.json is missing, so the marketplace cannot list this plugin")

    # ---- rendering -------------------------------------------------------
    width, height = (int(v) for v in args.size.lower().split("x"))
    source = make_test_frame(width, height)

    print("\nrendering")
    frames = sequence(effect, source, args.frames)
    ok(f"{args.frames} frames at {width}x{height}")

    changed = np.abs(frames[0][:, :, :3].astype(np.int16)
                     - source[:, :, :3].astype(np.int16))
    if changed.max() == 0:
        bad("the first frame is identical to the input - the effect drew nothing")
    else:
        ok(f"first frame differs from input (max delta {int(changed.max())}, "
           f"mean {changed.mean():.2f})")

    alpha_delta = np.abs(frames[0][:, :, 3].astype(np.int16)
                         - source[:, :, 3].astype(np.int16)).max()
    if alpha_delta:
        warn(f"the alpha channel was modified (max delta {int(alpha_delta)}); "
             f"hosts normally expect alpha to be left alone")

    # ---- determinism -----------------------------------------------------
    print("\ndeterminism")
    again = sequence(effect, source, args.frames)
    mismatch = next((i for i in range(args.frames)
                     if not np.array_equal(frames[i], again[i])), None)
    if mismatch is None:
        ok("a second pass renders byte-identical frames")
    else:
        bad(f"frame {mismatch} differs between two identical passes - the effect "
            f"uses rand()/time() instead of a seeded hash")

    # ---- motion and coherence -------------------------------------------
    print("\nmotion")
    luma = np.stack([f[:, :, :3].astype(np.float32).mean(axis=2) for f in frames])
    moving = luma - np.median(luma, axis=0)
    energy = float(np.abs(moving).mean())

    if energy < 0.25:
        ok(f"no per-frame animation (change energy {energy:.3f}) - static effect, "
           f"coherence check not applicable")
    else:
        near = [phase_shift(moving[t], moving[t + 1]) for t in range(args.frames - 1)]
        near_corr = float(np.mean([c for _, _, c in near]))
        dy = float(np.median([d for d, _, _ in near]))
        dx = float(np.median([d for _, d, _ in near]))

        gap = max(2, args.frames // 3)
        far_corr = float(np.mean([phase_shift(moving[t], moving[t + gap])[2]
                                  for t in range(args.frames - gap)]))

        ok(f"change energy {energy:.3f}, dominant motion {dx:+.0f} px/frame "
           f"horizontally, {dy:+.0f} px/frame vertically")
        print(f"  [info] correlation: neighbouring frames {near_corr:+.3f}, "
              f"{gap} frames apart {far_corr:+.3f}")

        if near_corr < 0.12:
            bad(f"neighbouring frames barely correlate ({near_corr:+.3f}): the effect "
                f"looks like per-frame noise, not motion")
        elif near_corr < far_corr * 2.0:
            bad(f"neighbouring frames ({near_corr:+.3f}) are no more alike than distant "
                f"ones ({far_corr:+.3f}): nothing is actually being carried between frames")
        else:
            ok(f"temporally coherent: neighbours correlate {near_corr / max(far_corr, 1e-6):.1f}x "
               f"better than distant frames")

    # ---- performance -----------------------------------------------------
    print("\nperformance")
    pw, ph = (int(v) for v in args.perf_size.lower().split("x"))
    big = make_test_frame(pw, ph)
    state = ctypes.c_void_p(None)
    try:
        effect.render(big, 0.0, state)          # warm up
        started = time.perf_counter()
        passes = 8
        for i in range(passes):
            effect.render(big, (i + 1) / passes, state)
        per_frame = (time.perf_counter() - started) / passes * 1000.0
    finally:
        effect.free_state(state)

    if per_frame > 120.0:
        warn(f"{per_frame:.1f} ms per {pw}x{ph} frame - slow enough to be felt when "
             f"scrubbing a timeline")
    else:
        ok(f"{per_frame:.1f} ms per {pw}x{ph} frame")

    # ---- verdict ---------------------------------------------------------
    print()
    if problems:
        print(f"FAILED: {len(problems)} problem(s)")
        for item in problems:
            print(f"  - {item}")
        sys.exit(1)

    if warnings:
        print(f"PASSED with {len(warnings)} warning(s)")
    else:
        print("PASSED")


if __name__ == "__main__":
    main()
