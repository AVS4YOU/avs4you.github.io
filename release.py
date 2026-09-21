from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile
import zlib
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


# slug, pluginId and version reach plugins.json, which shipped installers read
# with a hand-written Pascal parser, and pluginId names the folder a plugin is
# installed into. So all three stay plain ASCII: no spaces, quotes, commas or
# path separators. See docs/PluginsManifest.md.
IDENTITY_RULES = (
    ("slug", re.compile(r"[a-z0-9][a-z0-9-]{0,63}"),
     "lowercase letters, digits and '-', at most 64 characters"),
    ("pluginId", re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*"),
     "letters, digits, '.', '_' and '-'"),
    ("version", re.compile(r"[A-Za-z0-9._-]+"), "letters, digits, '.', '_' and '-'"),
)

# Installers also skip a record whose pluginId does not end in ".plugin",
# holds ".." or is longer than this, or whose download URL is longer than
# MAX_URL_LENGTH.
MAX_PLUGIN_ID_LENGTH = 128
MAX_URL_LENGTH = 1024
# ... and any download that is not under this prefix (PLUGINS_DOWNLOAD_PREFIX).
INSTALLER_URL_PREFIX = "https://github.com/AVS4YOU/"


def check_identity(config: dict, where: str) -> dict:
    """Validate the fields plugins.json consumers key on; return them stripped.

    Uniqueness across plugins is package.py's job - only it sees every config.
    """

    identity = {}
    for key, pattern, allowed in IDENTITY_RULES:
        value = config.get(key)
        if value is not None and not isinstance(value, str):
            # str() would quietly turn 1.10 into "1.1" and true into "True"
            raise ReleaseError(f"{where}: '{key}' must be a string, not {json.dumps(value)}")
        value = (value or "").strip()
        if not value:
            hint = " - the string the plugin DLL's PluginId() returns"
            raise ReleaseError(f"{where} has no '{key}'" + (hint if key == "pluginId" else ""))
        if not pattern.fullmatch(value):
            # ascii() makes a look-alike visible: a Cyrillic "o" prints as \u043e
            raise ReleaseError(f"{where}: '{key}' {ascii(value)} may only hold {allowed}")
        identity[key] = value

    plugin_id = identity["pluginId"]
    if (not plugin_id.endswith(".plugin") or ".." in plugin_id
            or len(plugin_id) > MAX_PLUGIN_ID_LENGTH):
        raise ReleaseError(
            f"{where}: 'pluginId' {plugin_id!r} must end in '.plugin', hold no '..' "
            f"and be at most {MAX_PLUGIN_ID_LENGTH} characters - installers skip it otherwise"
        )

    # The version is part of the tag <slug>-<version>, and GitHub will not
    # create a tag that git check-ref-format rejects.
    version = identity["version"]
    if ".." in version or version.endswith((".", ".lock")):
        raise ReleaseError(
            f"{where}: 'version' {version!r} is not a valid tag name - it may not "
            f"hold '..' or end in '.' or '.lock'"
        )

    for arch in ARCHS:
        url = asset_url(identity["slug"], identity["version"], arch)
        if (not url.isascii() or len(url) > MAX_URL_LENGTH
                or any(char.isspace() or char in "\"'" for char in url)):
            raise ReleaseError(
                f"{where}: download URL {url!r} must be ASCII, without spaces or "
                f"quotes, and at most {MAX_URL_LENGTH} characters"
            )
        if not url.startswith(INSTALLER_URL_PREFIX):
            raise ReleaseError(
                f"{where}: download URL {url!r} is not under {INSTALLER_URL_PREFIX} - "
                f"installers only download from there (RELEASE_REPO)"
            )

    return identity


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

    identity = check_identity(config, f"plugins/{name}/config.json")

    return {
        "folder": name,
        "dir": plugin_dir,
        "slug": identity["slug"],
        "pluginId": identity["pluginId"],
        "version": identity["version"],
        "title": str(config.get("name") or identity["slug"]),
    }


def package_has_plugin_id(package: Path, plugin_id: str) -> bool:
    """True if one of the DLLs inside the .avsp carries plugin_id.

    PluginId() returns a wide string literal, so the id sits in the DLL as
    UTF-16LE. Installers find an installed plugin by the pluginId published in
    plugins.json; if it drifts from the code they look in the wrong folder and
    offer the plugin again on every run.
    """

    # the whole NUL-terminated wide string: "VHS.plugin" must not match
    # "EffectVHS.plugin"
    needle = plugin_id.encode("utf-16-le") + b"\x00\x00"
    id_bytes = frozenset(
        b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")

    def holds_id(data: bytes) -> bool:
        index = data.find(needle)
        while index >= 0:
            before = data[index - 2:index] if index >= 2 else b""
            if not (len(before) == 2 and before[1] == 0 and before[0] in id_bytes):
                return True
            index = data.find(needle, index + 1)
        return False

    try:
        with zipfile.ZipFile(package) as archive:
            return any(
                holds_id(archive.read(member))
                for member in archive.namelist()
                if member.lower().endswith(".dll")
            )
    except (zipfile.BadZipFile, zlib.error, EOFError, OSError, RuntimeError,
            NotImplementedError) as error:
        # zipfile does not wrap everything a damaged archive raises: a bad
        # offset fails in seek() (OSError), a broken member in its decompressor
        # (zlib.error, EOFError), an encrypted one with RuntimeError and an
        # unknown compression method with NotImplementedError.
        raise ReleaseError(f"{package}: not a valid .avsp ({error})") from error


def package_is_plain_zip(package: Path) -> bool:
    """True for the only layout installers accept: a local file header at
    offset 0 and the end-of-central-directory record exactly 22 bytes before
    the end - no self-extracting stub, no archive comment."""

    size = package.stat().st_size
    if size < 64:  # the smallest file installers look at
        return False
    with package.open("rb") as stream:
        head = stream.read(4)
        stream.seek(size - 22)
        tail = stream.read(4)
    return head == b"PK\x03\x04" and tail == b"PK\x05\x06"


def resolve_packages(plugin: dict) -> None:
    plugin["packages"] = {
        arch: find_package(plugin["dir"], arch) for arch in ARCHS
    }
    for arch, package in plugin["packages"].items():
        if not package_is_plain_zip(package):
            raise ReleaseError(
                f"{plugin['folder']}: build/{arch}/{package.name} is not a plain ZIP "
                f"(an archive comment or a stub) - installers would reject it"
            )
        if not package_has_plugin_id(package, plugin["pluginId"]):
            raise ReleaseError(
                f"{plugin['folder']}: no DLL in build/{arch}/{package.name} contains "
                f"pluginId {plugin['pluginId']!r} - config.json disagrees with "
                f"PluginId(), or the package is stale"
            )


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
