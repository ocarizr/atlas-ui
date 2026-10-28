"""Build only a component's named targets and run its registered CTest labels."""

import argparse
import os
import re
import subprocess
from pathlib import Path

from scripts.ci.config import read_yaml
from scripts.ci.coverage import collect_coverage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--component", required=True)
    parser.add_argument("--configure", required=True)
    parser.add_argument("--configuration", required=True)
    args = parser.parse_args()
    config = read_yaml(".ci/components.yaml")
    component = config["components"][args.component]
    profile = {"configure": args.configure, "configuration": args.configuration}
    if profile not in config["profiles"]:
        raise ValueError("Unknown validation profile")
    targets, labels = component["build_targets"], component["test_labels"]
    if not targets or not labels:
        raise ValueError("Real build_targets and test_labels are required")
    build = Path("build") / args.configure
    subprocess.run(["cmake", "--preset", args.configure], check=True)
    subprocess.run(
        ["cmake", "--build", str(build), "--config", args.configuration, "--target", *targets],
        check=True,
    )
    expression = "^(?:" + "|".join(re.escape(label) for label in labels) + ")$"
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
