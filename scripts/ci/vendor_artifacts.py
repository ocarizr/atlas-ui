"""Discover compatible, unexpired dependency products from trusted main producers."""

import argparse
import json
import os
from urllib.parse import quote
from urllib.request import Request, urlopen

from scripts.ci.config import write_outputs


def producer_succeeded(fetch, repository, run_id, group):
    """Check the selected vendor producer job rather than unrelated workflow results."""
    page = 1
    while True:
        jobs = fetch(f"/repos/{repository}/actions/runs/{run_id}/jobs?page={page}&per_page=100")[
            "jobs"
        ]
        if any(
            job["name"].split(" / ")[-1] == f"vendor-{group}"
            and job.get("status") == "completed"
            and job.get("conclusion") == "success"
            for job in jobs
        ):
            return True
        if len(jobs) < 100:
            return False
        page += 1


def find_product(fetch, repository, group, key, current_run):
    """Search trusted compatible vendor artifacts, following catalog and producer-job pages."""
    name = f"vendor-{group}-{key}"
    page = 1
    while True:
        catalog = fetch(
            f"/repos/{repository}/actions/artifacts?name={quote(name)}&page={page}&per_page=100"
        )
        artifacts = catalog["artifacts"]
        for artifact in artifacts:
            if artifact.get("expired") or artifact["name"] != name:
                continue
            run_id = artifact.get("workflow_run", {}).get("id")
            if not run_id or run_id == current_run:
                continue
            run = fetch(f"/repos/{repository}/actions/runs/{run_id}")
            if (
                run.get("head_branch") != "main"
                or run.get("event") != "push"
                or run.get("path") != ".github/workflows/delivery.yml"
                or run.get("head_repository", {}).get("full_name") != repository
            ):
                continue
            if producer_succeeded(fetch, repository, run_id, group):
                return run_id
        if len(artifacts) < 100:
            return None
        page += 1


def github_fetch(path):
    """Request authenticated GitHub JSON for artifact discovery without logging credentials."""
    request = Request(
        "https://api.github.com" + path,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": "Bearer " + os.environ["GH_TOKEN"],
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    with urlopen(request, timeout=60) as response:
        return json.load(response)


def main():
    """Find and download the selected trusted compatible vendor product when available."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--github-output", required=True)
    args = parser.parse_args()
    run_id = find_product(
        github_fetch,
        os.environ["GITHUB_REPOSITORY"],
        args.group,
        args.key,
        int(os.environ["GITHUB_RUN_ID"]),
    )
    write_outputs({"run-id": str(run_id) if run_id else ""}, args.github_output)


if __name__ == "__main__":
    main()
