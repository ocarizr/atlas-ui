"""Build selected named targets once per profile and run their CTest labels."""

import argparse
import os
import re
import subprocess
from pathlib import Path

from scripts.ci.config import read_yaml
from scripts.ci.coverage import collect_coverage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--component", required=True, nargs="+")
    parser.add_argument("--configure", required=True)
    parser.add_argument("--configuration", required=True)
    args = parser.parse_args()
    config = read_yaml(".ci/components.yaml")
    profile = next((item for item in config["profiles"]
                    if item["configure"] == args.configure and item["configuration"] == args.configuration), None)
    if profile is None:
        raise ValueError("Unknown validation profile")
    if set(args.component).intersection(profile.get("exclude_components", [])):
        raise ValueError("Selected component is excluded by this profile")
    selected = [config["components"][name] for name in args.component]
    if any(not item["build_targets"] or not item["test_labels"] for item in selected):
        raise ValueError("Every selected component needs build targets and test labels")
    targets = sorted({target for item in selected for target in item["build_targets"]})
    labels = sorted({label for item in selected for label in item["test_labels"]})
    if not targets or not labels:
        raise ValueError("Real build_targets and test_labels are required")
    build = Path("build") / args.configure
    subprocess.run(["cmake", "--preset", args.configure], check=True)
    subprocess.run(
        ["cmake", "--build", str(build), "--config", args.configuration, "--target", *targets],
        check=True,
    )
    expression = "^(" + "|".join(re.escape(label) for label in labels) + ")$"
    environment = os.environ.copy()
    profiles = build / "coverage"
    if args.configure == "windows-coverage":
        profiles.mkdir(parents=True, exist_ok=True)
        for previous in profiles.glob("run-*.profraw"):
            previous.unlink()
        environment["LLVM_PROFILE_FILE"] = str(profiles.resolve() / "run-%m-%p.profraw")
    subprocess.run(
        ["ctest", "--test-dir", str(build), "-C", args.configuration,
         "--output-on-failure", "--no-tests=error", "-L", expression],
        check=True, env=environment,
    )
    if args.configure == "windows-coverage":
        collect_coverage(build, args.configuration, expression, profiles)


if __name__ == "__main__":
    main()
