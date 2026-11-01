"""Publish a verified complete Doxygen snapshot to the repository's dedicated wiki."""

import argparse
import base64
import os
import re
import subprocess
import tempfile
from pathlib import Path

from scripts.ci.wiki_snapshot import read_snapshot, synchronize


def git(arguments, environment, directory=None):
    """Run Git without shell interpolation or printing authentication configuration."""
    command = ["git"] + (["-C", str(directory)] if directory else []) + arguments
    return subprocess.check_output(command, env=environment, text=True).strip()


def authentication(token):
    """Pass HTTPS credentials through process configuration rather than URLs or arguments."""
    environment = os.environ.copy()
    environment["GIT_TERMINAL_PROMPT"] = "0"
    if token:
        count = int(environment.get("GIT_CONFIG_COUNT", "0"))
        credentials = base64.b64encode(f"x-access-token:{token}".encode()).decode()
        environment[f"GIT_CONFIG_KEY_{count}"] = "http.https://github.com/.extraheader"
        environment[f"GIT_CONFIG_VALUE_{count}"] = f"AUTHORIZATION: basic {credentials}"
        environment["GIT_CONFIG_COUNT"] = str(count + 1)
    return environment


def publish(source, repository, ssh=False, current_main=False):
    """Clone, validate, synchronize and push normally; never force-push wiki history."""
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Expected repository owner/name")
    manifest, _ = read_snapshot(source)
    token = os.environ.get("WIKI_TOKEN")
    if not ssh and not token:
        raise ValueError("Configure the Actions secret WIKI_TOKEN with wiki write access")
    environment = authentication(token if not ssh else None)
    prefix = "git@github.com:" if ssh else "https://github.com/"
    if current_main:
        latest = git(["ls-remote", f"{prefix}{repository}.git", "refs/heads/main"], environment)
        if latest.split()[0] != manifest["revision"]:
            print("Skipping superseded source revision; main has advanced.")
            return
    with tempfile.TemporaryDirectory(prefix="atlas-wiki-") as temporary:
        wiki = Path(temporary) / "wiki"
        git(["clone", f"{prefix}{repository}.wiki.git", str(wiki)], environment)
        synchronize(source, wiki)
        git(["add", "--all"], environment, wiki)
        if not git(["diff", "--cached", "--name-only"], environment, wiki):
            print("Wiki already matches the generated snapshot.")
            return
        git(["config", "user.name", "github-actions[bot]"], environment, wiki)
        git(
            ["config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"],
            environment,
            wiki,
        )
        message = f"Update Doxygen reference for {manifest['revision'][:12]}\n\n"
        message += (
            "Purpose:\nKeep the public API wiki aligned with the documented source revision.\n\n"
        )
        message += "Changes:\nReplace the generated reference and remove obsolete pages.\n\n"
        message += "Validation:\nVerified snapshot checksums and all generated page/anchor links before synchronization."
        git(["commit", "-m", message], environment, wiki)
        git(["push", "origin", "HEAD"], environment, wiki)
        print(f"Published {len(manifest['pages'])} pages to https://github.com/{repository}/wiki")


def main():
    """Publish in CI with WIKI_TOKEN, or locally using explicitly selected SSH credentials."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", default="build/documentation/wiki")
    parser.add_argument("--repository", required=True)
    parser.add_argument("--ssh", action="store_true")
    parser.add_argument("--current-main", action="store_true")
    args = parser.parse_args()
    publish(Path(args.source).resolve(), args.repository, args.ssh, args.current_main)


if __name__ == "__main__":
    main()
