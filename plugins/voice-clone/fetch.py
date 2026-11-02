"""Build the voice conversion engine Voice Clone drives.

    python fetch.py              seedvc.exe into build/x64/seedvc/ (once)
    python fetch.py --clean      rebuild it from scratch
    python fetch.py --check      change nothing, exit 1 if it is missing

engine/seedvc.py (Seed-VC on ONNX Runtime, GPL-3.0) becomes a standalone
program with PyInstaller, in its own virtual environment under build/ with
pinned numpy and onnxruntime-directml: DirectML runs it on any DirectX 12
graphics card, and the CPU otherwise. The plugin runs it as a separate process,
which keeps the GPL to the engine. PyInstaller copies the C++ runtime of the
Python it runs on, older than ONNX Runtime needs ("DLL initialization routine
failed"): the build machine's current one replaces it. Next to seedvc.exe go
its licence (LICENSE, GPL-3.0), where its source is (README.txt) and the
licences of the libraries packed with it (THIRD-PARTY-NOTICES.txt).

There is no 32-bit engine: the models need more memory than a 32-bit process
has. The x86 package carries the x64 engine and runs it on 64-bit Windows.

The models are not built here: the plugin downloads them on first use
(engine/export_onnx.py makes them from Seed-VC's checkpoints).
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

PLUGIN_DIR = Path(__file__).resolve().parent
ENGINE = PLUGIN_DIR / "engine" / "seedvc.py"
OUTPUT = PLUGIN_DIR / "build" / "x64" / "seedvc"
WORK = PLUGIN_DIR / "build" / "engine"
REQUIREMENTS = ["numpy==1.26.4", "onnxruntime-directml==1.22.0", "pyinstaller==6.11.1"]
DIRECTML = "1.15.4"  # the DirectML.dll onnxruntime-directml 1.22.0 ships
RUNTIME = ["msvcp140.dll", "msvcp140_1.dll", "vcruntime140.dll", "vcruntime140_1.dll"]
SOURCE = "https://github.com/AVS4YOU/avs4you.github.io/tree/master/plugins/voice-clone"

README = """\
seedvc.exe - the voice conversion engine of the Voice Clone plugin for AVS4YOU
==============================================================================

It is Seed-VC (https://github.com/Plachtaa/seed-vc) running on ONNX Runtime,
and like Seed-VC it is free software under the GNU General Public License
version 3 (LICENSE in this folder). The plugin itself only starts this program.

Source code - the Corresponding Source of GPL-3.0 section 6:
  {source}
    engine/seedvc.py        this program
    engine/export_onnx.py   makes the models it runs from Seed-VC's checkpoints
    fetch.py                builds this folder: PyInstaller on Python {python}
                            with {requirements}
  Every published version of the plugin is tagged voice-clone-v<version> there.

The models it runs and their licences: ..\\licenses\\MODELS-LICENSES.txt
The libraries packed with it: THIRD-PARTY-NOTICES.txt
"""

NOTICES = """\
seedvc.exe - the libraries packed with it
=========================================

seedvc.exe is a Python program frozen with PyInstaller; the folder _internal
holds what it runs on:

- Python {python} - PSF License Agreement, with the licences of the libraries
  the Windows build of Python includes (OpenSSL, libffi, bzip2 and others):
  below.
- {numpy} - BSD 3-Clause, with OpenBLAS and the other libraries in
  numpy.libs: below.
- ONNX Runtime ({onnxruntime}) - MIT License, with its third-party notices:
  below.
- DirectML.dll {directml} - (c) Microsoft Corporation, shipped with ONNX Runtime
  under the Microsoft Software License Terms for DirectML:
  https://www.nuget.org/packages/Microsoft.AI.DirectML/{directml}/License
- The PyInstaller bootloader (the start of seedvc.exe) - GPL 2.0 with the
  PyInstaller bootloader exception, which allows combining it with any
  program and distributing the combination without restriction:
  https://github.com/pyinstaller/pyinstaller/blob/develop/COPYING.txt
- The Microsoft C and C++ runtime (vcruntime140*.dll, msvcp140*.dll,
  ucrtbase.dll, api-ms-win-*.dll) - redistributable files of Microsoft Visual
  Studio and the Windows SDK.
"""


def stamp() -> str:
    digest = hashlib.sha256(ENGINE.read_bytes())
    digest.update(" ".join(REQUIREMENTS).encode())
    digest.update(Path(__file__).read_bytes())  # the notices it writes
    return digest.hexdigest()


def write_text(path: Path, text: str) -> None:
    """UTF-8 with BOM and CRLF, so Notepad shows it right on any Windows"""
    path.write_bytes(b"\xef\xbb\xbf" + text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))


def write_notices(python: Path) -> None:
    """Next to seedvc.exe: where its source is, as GPL-3.0 section 6 asks, and
    the licences of what PyInstaller packed with it, from the venv it was built in."""
    def ask(code: str) -> str:
        return subprocess.run([str(python), "-c", code], check=True, capture_output=True, text=True).stdout.strip()
    version, base = ask("import platform; print(platform.python_version())"), Path(ask("import sys; print(sys.base_prefix)"))
    site = WORK / "venv" / "Lib" / "site-packages"
    write_text(OUTPUT / "README.txt", README.format(source=SOURCE, python=version, requirements=", ".join(REQUIREMENTS)))
    numpy, onnxruntime = (next(r for r in REQUIREMENTS if r.startswith(name)) for name in ("numpy", "onnxruntime"))
    parts = [NOTICES.format(python=version, numpy=numpy, onnxruntime=onnxruntime, directml=DIRECTML)]
    for title, path in [(f"Python {version}", base / "LICENSE.txt"),
                        (numpy, next(site.glob("numpy-*.dist-info")) / "LICENSE.txt"),
                        (f"ONNX Runtime ({onnxruntime})", site / "onnxruntime" / "LICENSE"),
                        ("ONNX Runtime - third-party notices", site / "onnxruntime" / "ThirdPartyNotices.txt")]:
        rule = "=" * 80
        parts.append(f"\n{rule}\n{title}\n{rule}\n\n{path.read_text(encoding='utf-8-sig', errors='replace').strip()}\n")
    write_text(OUTPUT / "THIRD-PARTY-NOTICES.txt", "".join(parts))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fetch nothing; exit 1 if the engine is missing")
    parser.add_argument("--clean", action="store_true", help="rebuild from scratch")
    # what build.py passes every fetch.py; there is one engine for both packages
    parser.add_argument("--arch", choices=["x86", "x64", "both"], default="both", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)

    exe = OUTPUT / "seedvc.exe"
    marker = WORK / "seedvc.stamp"  # not in OUTPUT: that goes into the package as it is
    current = exe.is_file() and marker.is_file() and marker.read_text() == stamp()
    if args.check:
        print(("ok:      " if current else "missing: ") + str(exe))
        return 0 if current else 1
    if current and not args.clean:
        print(f"ok:      {exe}")
        return 0

    if args.clean and WORK.exists():
        shutil.rmtree(WORK)
    venv = WORK / "venv"
    python = venv / "Scripts" / "python.exe"
    if not python.is_file():
        subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
        subprocess.run([str(python), "-m", "pip", "install", "-q", "--upgrade", "pip"], check=True)
    subprocess.run([str(python), "-m", "pip", "install", "-q", *REQUIREMENTS], check=True)

    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    subprocess.run([str(venv / "Scripts" / "pyinstaller.exe"), "--noconfirm", "--onedir", "--console",
                    "--name", "seedvc", "--distpath", str(OUTPUT.parent), "--workpath", str(WORK / "pyinstaller"),
                    "--specpath", str(WORK), "--hidden-import", "secrets", "--exclude-module", "tkinter",
                    str(ENGINE)], check=True)
    system32 = Path(r"C:\Windows\System32")
    for name in RUNTIME:
        shutil.copy2(system32 / name, OUTPUT / "_internal" / name)
    shutil.copy2(PLUGIN_DIR / "engine" / "LICENSE", OUTPUT / "LICENSE")
    write_notices(python)
    marker.write_text(stamp())
    print(f"built:   {exe}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
