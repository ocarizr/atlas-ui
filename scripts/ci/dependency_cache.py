"""Identify additions to the content-addressed vcpkg binary archive cache."""

import argparse
import hashlib
from pathlib import Path

from scripts.ci.config import write_outputs


def inventory(root):
    """List completed vcpkg binary archives with sizes for cache-progress diagnostics."""
    root = Path(root)
    archives = sorted(
        f"{path.relative_to(root).as_posix()}:{path.stat().st_size}"
        for path in root.rglob("*.zip")
        if path.is_file()
    )
    fingerprint = hashlib.sha256("\n".join(archives).encode("utf-8")).hexdigest()
    return {"fingerprint": fingerprint, "has-archives": bool(archives), "archives": len(archives)}


def main():
    """Report completed vendor archives and save cache-delta metadata for CI."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".tools/vcpkg-cache")
    parser.add_argument("--github-output")
    args = parser.parse_args()
    snapshot = inventory(args.root)
    write_outputs(snapshot, args.github_output)
    print(
        f"Dependency cache: {snapshot['archives']} complete binary archives; {snapshot['fingerprint']}"
    )


if __name__ == "__main__":
    main()
