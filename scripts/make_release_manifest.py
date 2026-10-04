#!/usr/bin/env python3
"""Write release.toml for a directory of release zips (dev-cli.md section 4.2).

Zip names are the contract:
  <project>-<version>-<platform>[-debug].zip  -> kind "game"
  vb-<version>-<platform>.zip                 -> kind "cli"
Every listed file gets its size and SHA-256. Standard library only.
"""
import argparse
import datetime
import hashlib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAME_RE = re.compile(
    r"^(?P<prefix>.+?)-(?P<version>v\d+\.\d+\.\d+)-"
    r"(?P<platform>(?:linux|macos|windows)-[a-z0-9_]+?)(?P<debug>-debug)?\.zip$")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def protocol_version():
    text = (ROOT / "cmake" / "version.hpp.in").read_text()
    m = re.search(r"kEngineProtocolVersion\s*=\s*(\d+)", text)
    if not m:
        sys.exit("make_release_manifest: kEngineProtocolVersion not found")
    return int(m.group(1))


def git(*args):
    try:
        return subprocess.check_output(["git", *args], cwd=ROOT, text=True,
                                       stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def collect(directory, project):
    artifacts = []
    for path in sorted(directory.glob("*.zip")):
        m = NAME_RE.match(path.name)
        if not m:
            sys.exit(f"make_release_manifest: unrecognised archive name {path.name}")
        if m["prefix"] == project:
            kind = "game"
        elif m["prefix"] == "vb":
            kind = "cli"
            if m["debug"]:
                sys.exit(f"make_release_manifest: {path.name}: the CLI has no debug build")
        else:
            sys.exit(f"make_release_manifest: unknown archive prefix in {path.name}")
        artifacts.append(dict(kind=kind, version=m["version"], platform=m["platform"],
                              build="debug" if m["debug"] else "release", file=path.name,
                              size=path.stat().st_size, sha256=sha256(path)))
    return artifacts


def render(artifacts, version, commit, date, protocol):
    out = ["schema  = 1", f'version = "{version}"', f'commit  = "{commit}"',
           f'date    = "{date}"', f"engine_protocol_version = {protocol}", ""]
    for a in artifacts:
        out += ["[[artifact]]", f'kind     = "{a["kind"]}"', f'platform = "{a["platform"]}"',
                f'build    = "{a["build"]}"', f'file     = "{a["file"]}"',
                f'size     = {a["size"]}', f'sha256   = "{a["sha256"]}"', ""]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dir", required=True, type=Path, help="directory holding the zips")
    ap.add_argument("--project", default=(ROOT / "PROJECT_NAME").read_text().strip())
    ap.add_argument("--commit", default=git("rev-parse", "HEAD") or "unknown")
    ap.add_argument("--out", type=Path, help="default: <dir>/release.toml")
    args = ap.parse_args()

    artifacts = collect(args.dir, args.project)
    if not artifacts:
        sys.exit(f"make_release_manifest: no zips in {args.dir}")
    versions = {a["version"] for a in artifacts}
    if len(versions) != 1:
        sys.exit(f"make_release_manifest: mixed versions {sorted(versions)}")
    keys = [(a["kind"], a["platform"], a["build"]) for a in artifacts]
    if len(keys) != len(set(keys)):
        sys.exit("make_release_manifest: duplicate platform/build artifacts")
    date = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    text = render(artifacts, versions.pop(), args.commit, date, protocol_version())
    out = args.out or args.dir / "release.toml"
    out.write_text(text + "\n")
    print(f"wrote {out} ({len(artifacts)} artifacts)")


if __name__ == "__main__":
    main()
