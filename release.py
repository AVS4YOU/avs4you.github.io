from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PLUGINS_DIR = ROOT / "plugins"

RELEASE_REPO = "AVS4YOU/avs4you.github.io"
ARCHS = ("x86", "x64")

API_ROOT = "https://api.github.com"
API_VERSION = "2022-11-28"


# --- naming rules -----------------------------------------------------------
# The single source of truth for release naming. package.py imports asset_url()
# so the links baked into index.html and the assets published here can never
# drift apart.


def tag_for(slug: str, version: str) -> str:
    return f"{slug}-{version}"


def asset_name(slug: str, version: str, arch: str) -> str:
    return f"{slug}-{version}-{arch}.avsp"


def asset_url(slug: str, version: str, arch: str, repo: str = RELEASE_REPO) -> str:
    return (
        f"https://github.com/{repo}/releases/download/"
        f"{tag_for(slug, version)}/{asset_name(slug, version, arch)}"
    )


class ReleaseError(Exception):
    """A problem the user has to fix before anything is published."""


# --- github api -------------------------------------------------------------


def github_token() -> str:
    for name in ("GITHUB_TOKEN", "GH_TOKEN"):
        value = os.environ.get(name)
        if value and value.strip():
            return value.strip()

    try:
        result = subprocess.run(
            ["gh", "auth", "token"],
            capture_output=True,
            text=True,
            timeout=20,
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass

    raise ReleaseError(
        "no GitHub token: set GITHUB_TOKEN (classic token with 'repo' scope, or "
        "fine-grained with Contents: read and write), or install gh and run "
        "'gh auth login'"
    )


def api_request(
    method: str,
    url: str,
    token: str | None,
    *,
    payload: dict | None = None,
    body: object | None = None,
    content_type: str | None = None,
    content_length: int | None = None,
) -> tuple[int, dict]:
    """Call the GitHub API. Returns (status, decoded json or {})."""

    data: object | None = body
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": API_VERSION,
        "User-Agent": "avs4you-release-script",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if content_type:
        headers["Content-Type"] = content_type
    if content_length is not None:
        headers["Content-Length"] = str(content_length)

    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request) as response:
            raw = response.read()
            return response.status, (json.loads(raw) if raw else {})
    except urllib.error.HTTPError as error:
        raw = error.read()
        try:
            decoded = json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            decoded = {"message": raw.decode("utf-8", "replace")}
        return error.code, decoded


def api_error(status: int, decoded: dict) -> str:
    message = decoded.get("message", "unknown error")
    details = decoded.get("errors")
    if details:
        message = f"{message} ({json.dumps(details)})"
    return f"HTTP {status}: {message}"


def find_release(repo: str, tag: str, token: str | None) -> dict | None:
    status, decoded = api_request(
        "GET", f"{API_ROOT}/repos/{repo}/releases/tags/{tag}", token
    )
    if status == 200:
        return decoded
    if status == 404:
        return None
    raise ReleaseError(f"cannot query release {tag}: {api_error(status, decoded)}")


def create_release(repo: str, tag: str, target: str, name: str, token: str) -> dict:
    status, decoded = api_request(
        "POST",
        f"{API_ROOT}/repos/{repo}/releases",
        token,
        payload={
            "tag_name": tag,
            "target_commitish": target,
            "name": name,
            "draft": False,
            "prerelease": False,
        },
    )
    if status != 201:
        raise ReleaseError(f"cannot create release {tag}: {api_error(status, decoded)}")
    return decoded


def upload_asset(upload_url: str, path: Path, name: str, token: str) -> str:
    # upload_url comes back from the API as an RFC 6570 template ending in
    # "{?name,label}". Uploading to the URL the API handed us keeps the request
    # off any redirect - a redirected POST could not replay the file stream.
    size = path.stat().st_size
    url = f"{upload_url.split('{')[0]}?name={name}"
    with path.open("rb") as stream:
        status, decoded = api_request(
            "POST",
            url,
            token,
            body=stream,
            content_type="application/octet-stream",
            content_length=size,
        )
    if status != 201:
        raise ReleaseError(f"cannot upload {name}: {api_error(status, decoded)}")
    return decoded["browser_download_url"]


def delete_release(repo: str, release_id: int, token: str) -> None:
    api_request("DELETE", f"{API_ROOT}/repos/{repo}/releases/{release_id}", token)


# --- plugins ----------------------------------------------------------------


def find_package(plugin_dir: Path, arch: str) -> Path:
    build_dir = plugin_dir / "build" / arch
    if not build_dir.is_dir():
        raise ReleaseError(f"{plugin_dir.name}: no build/{arch} directory - build it first")

    matches = sorted(build_dir.glob("*.avsp"))
    if not matches:
        raise ReleaseError(f"{plugin_dir.name}: no .avsp in build/{arch}")
    if len(matches) > 1:
        names = ", ".join(item.name for item in matches)
        raise ReleaseError(f"{plugin_dir.name}: build/{arch} holds several .avsp ({names})")
    return matches[0]


def load_plugin(name: str) -> dict:
    plugin_dir = PLUGINS_DIR / name
    if not plugin_dir.is_dir():
        raise ReleaseError(f"{name}: no such plugin folder in plugins/")

    config_path = plugin_dir / "config.json"
    if not config_path.is_file():
        raise ReleaseError(f"{name}: no config.json")

    try:
        config = json.loads(config_path.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as error:
        raise ReleaseError(f"{name}: config.json is not valid JSON ({error})") from error

    slug = str(config.get("slug") or "").strip()
    if not slug:
        raise ReleaseError(f"{name}: config.json has no slug")

    version = str(config.get("version") or "").strip()
    if not version:
        raise ReleaseError(f"{name}: config.json has no version")

    return {
        "folder": name,
        "dir": plugin_dir,
        "slug": slug,
        "version": version,
        "title": str(config.get("name") or slug),
    }


def resolve_packages(plugin: dict) -> None:
    plugin["packages"] = {
        arch: find_package(plugin["dir"], arch) for arch in ARCHS
    }


def all_plugin_names() -> list[str]:
    return [
        item.name
        for item in sorted(PLUGINS_DIR.iterdir(), key=lambda entry: entry.name.lower())
        if item.is_dir() and (item / "config.json").is_file()
    ]


# --- publishing -------------------------------------------------------------


def publish(plugin: dict, repo: str, target: str, token: str) -> None:
    slug, version = plugin["slug"], plugin["version"]
    tag = tag_for(slug, version)

    release = create_release(repo, tag, target, f"{plugin['title']} {version}", token)
    print(f"  created release {tag}")

    try:
        for arch in ARCHS:
            source = plugin["packages"][arch]
            name = asset_name(slug, version, arch)
            size_mb = source.stat().st_size / (1024 * 1024)
            print(f"  uploading {name} ({size_mb:.1f} MiB) ...", flush=True)
            url = upload_asset(release["upload_url"], source, name, token)
            print(f"    {url}")
    except BaseException:
        # A release carrying only half its assets is worse than no release:
        # the URL in index.html would 404 for one architecture, silently.
        print(f"  upload failed - removing incomplete release {tag}", file=sys.stderr)
        delete_release(repo, release["id"], token)
        raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Publish built plugin packages as GitHub releases.",
        epilog="examples: python release.py StableAudio3 nano-banana | python release.py --all",
    )
    parser.add_argument(
        "plugins", nargs="*", metavar="PLUGIN", help="plugin folder name"
    )
    parser.add_argument(
        "--all", action="store_true", help="every plugin folder that has a config.json"
    )
    parser.add_argument(
        "--repo", default=RELEASE_REPO, help=f"target repository (default: {RELEASE_REPO})"
    )
    parser.add_argument(
        "--target", default="master", help="branch the tag is cut from (default: master)"
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="validate and print, publish nothing"
    )
    args = parser.parse_args(argv)

    if args.all and args.plugins:
        parser.error("--all takes no plugin names")
    if not args.all and not args.plugins:
        parser.error("name at least one plugin, or pass --all")

    try:
        names = all_plugin_names() if args.all else args.plugins
        plugins = [load_plugin(name) for name in names]

        # A dry run still authenticates when it can: an unauthenticated GET
        # against a private repo answers 404, which would read as "version is
        # free" when the release actually exists.
        if args.dry_run:
            try:
                token = github_token()
            except ReleaseError:
                token = None
                print("note: no token - private repos will look empty", file=sys.stderr)
        else:
            token = github_token()

        # Partition first. Whether a plugin is built only matters for the ones
        # we are about to publish - an unbuilt plugin sitting at an already
        # released version is none of our business.
        pending = []
        skipped = []
        for plugin in plugins:
            tag = tag_for(plugin["slug"], plugin["version"])
            if find_release(args.repo, tag, token) is not None:
                skipped.append(plugin)
                print(f"skip: {plugin['folder']} - {tag} is already released")
            else:
                pending.append(plugin)

        # Resolve every package before uploading any, so a plugin missing its
        # x64 build cannot leave an earlier plugin already published.
        for plugin in pending:
            resolve_packages(plugin)
    except ReleaseError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if not pending:
        print(f"nothing to publish - all {len(skipped)} plugin(s) are up to date")
        return 0

    for plugin in pending:
        tag = tag_for(plugin["slug"], plugin["version"])
        print(f"{plugin['folder']} -> {tag}")

        if args.dry_run:
            for arch in ARCHS:
                source = plugin["packages"][arch]
                size_mb = source.stat().st_size / (1024 * 1024)
                print(f"  would upload {source} ({size_mb:.1f} MiB)")
                print(f"    as {asset_name(plugin['slug'], plugin['version'], arch)}")
                print(f"    -> {asset_url(plugin['slug'], plugin['version'], arch, args.repo)}")
            continue

        try:
            publish(plugin, args.repo, args.target, token)
        except ReleaseError as error:
            print(f"error: {error}", file=sys.stderr)
            return 1

    published = ", ".join(item["folder"] for item in pending)
    verb = "would publish" if args.dry_run else "published"
    print()
    print(f"{verb} {len(pending)}: {published}")
    if skipped:
        print(f"skipped {len(skipped)} already-released: "
              + ", ".join(item["folder"] for item in skipped))
    if not args.dry_run:
        print("now run: python package.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
