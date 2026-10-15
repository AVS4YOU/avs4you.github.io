"""Restore the NuGet packages Supertonic3.vcxproj imports, without nuget.exe.

    python fetch.py            unpack whatever is missing
    python fetch.py --check    fetch nothing, exit 1 if a package is missing

What 'nuget restore' does for Supertonic3/packages.config: each package is
unpacked into Supertonic3/packages/<id>.<version>, where the project imports
its .targets from and build.py takes onnxruntime.dll for the package.
Present packages are left alone. build.py in the repository root runs this first.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ElementTree
import zipfile
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent / "Supertonic3"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true",
                        help="fetch nothing; exit 1 if a package is missing")
    # what build.py passes every fetch.py; a package holds both architectures
    # and a version never changes, so neither changes anything here
    parser.add_argument("--arch", choices=["x86", "x64", "both"], default="both",
                        help=argparse.SUPPRESS)
    parser.add_argument("--clean", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)

    missing = 0
    for package in ElementTree.parse(PROJECT_DIR / "packages.config").getroot().iter("package"):
        package_id, version = package.get("id"), package.get("version")
        target = PROJECT_DIR / "packages" / f"{package_id}.{version}"
        if target.is_dir():
            print(f"ok:      {package_id} {version}")
            continue
        if args.check:
            print(f"missing: {package_id} {version}")
            missing += 1
            continue

        url = f"https://www.nuget.org/api/v2/package/{package_id}/{version}"
        print(f"fetching {package_id} {version} from nuget.org ...", flush=True)
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=target.parent) as scratch:
            nupkg = Path(scratch) / "package.nupkg"
            try:
                urllib.request.urlretrieve(url, nupkg)
            except OSError as error:
                print(f"error: cannot download {url}: {error}", file=sys.stderr)
                return 1
            unpacked = Path(scratch) / "unpacked"
            with zipfile.ZipFile(nupkg) as archive:
                archive.extractall(unpacked)
            # renamed only once complete, so an interrupted fetch is retried
            unpacked.rename(target)
        print(f"ok:      {package_id} {version}")

    if missing:
        print("run: python fetch.py", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
