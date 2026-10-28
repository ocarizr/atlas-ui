"""Validate version tags and their membership in integration-branch history."""

import argparse
import re
import subprocess
from pathlib import Path

from scripts.ci.config import write_outputs

VERSION = re.compile(r"[vV]((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*))")


def version_from_tag(tag):
    match = VERSION.fullmatch(tag)
    if not match:
        raise ValueError("Expected vMAJOR.MINOR.PATCH or VMAJOR.MINOR.PATCH without leading zeros")
    return match.group(1)


def evaluate_ref(ref, main_ref, root=Path.cwd()):
    if ref == "refs/heads/main":
        return {"publish": False, "version": "development", "tag": ""}
    if not ref.startswith("refs/tags/"):
        raise ValueError("Delivery is restricted to main or version tags")
    tag = ref.removeprefix("refs/tags/")
    version = version_from_tag(tag)

    def git(*arguments):
        return subprocess.run(
            ["git", *arguments], cwd=root, check=True, capture_output=True, text=True
        ).stdout.strip()

    commit = git("rev-parse", f"refs/tags/{tag}^{{commit}}")
    if git("rev-parse", "HEAD") != commit:
        raise ValueError("The checkout does not match the release tag commit")
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, main_ref], cwd=root,
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        raise ValueError("The tag commit is not in main history (or main could not be resolved)")
    return {"publish": True, "version": version, "tag": tag}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--main-ref", default="origin/main")
    parser.add_argument("--github-output")
    args = parser.parse_args()
    result = evaluate_ref(args.ref, args.main_ref)
    write_outputs(result, args.github_output)
    print(f"Delivery version: {result['version']}; publish: {result['publish']}")


if __name__ == "__main__":
    main()
