"""Publish files a plugin downloads at run time - models - as a GitHub release.

    python tools/publish_assets.py <tag> <file>... [--dry-run]

Plugin packages go through release.py; this is for the large files they fetch
on first use, which do not belong in a package. The release lands in the same
repository (RELEASE_REPO), so the download links stay under our control:

    https://github.com/<RELEASE_REPO>/releases/download/<tag>/<file name>

An existing tag is never overwritten: publish changed files under a new tag
and point the plugin at it. Uses the token release.py uses (GITHUB_TOKEN,
GH_TOKEN or gh auth token).
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from release import (RELEASE_REPO, ReleaseError, create_release, delete_release,  # noqa: E402
                     find_release, github_token, upload_asset)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tag")
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--dry-run", action="store_true", help="validate and print, publish nothing")
    args = parser.parse_args(argv)

    missing = [str(path) for path in args.files if not path.is_file()]
    if missing:
        print(f"error: no such file: {', '.join(missing)}", file=sys.stderr)
        return 1

    try:
        token = github_token()
        if find_release(RELEASE_REPO, args.tag, token) is not None:
            raise ReleaseError(f"{args.tag} already exists - publish under a new tag")
        for path in args.files:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            print(f"{path.name}: {path.stat().st_size} bytes, sha256 {digest}")
            print(f"  -> https://github.com/{RELEASE_REPO}/releases/download/{args.tag}/{path.name}")
        if args.dry_run:
            return 0

        release = create_release(RELEASE_REPO, args.tag, "master", args.tag, token)
        try:
            for path in args.files:
                print(f"uploading {path.name} ...", flush=True)
                upload_asset(release["upload_url"], path, path.name, token)
        except BaseException:
            print(f"upload failed - removing incomplete release {args.tag}", file=sys.stderr)
            delete_release(RELEASE_REPO, release["id"], token)
            raise
    except ReleaseError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"published {args.tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
