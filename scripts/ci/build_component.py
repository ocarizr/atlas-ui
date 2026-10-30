"""Build selected named targets once per profile and run their CTest labels."""

import argparse
import os
import re
import subprocess
from pathlib import Path

from scripts.ci.config import read_yaml
from scripts.ci.configure import configure_command
from scripts.ci.coverage import collect_coverage
from scripts.ci.parallel_plan import plan_components
from scripts.ci.owned_products import validate_products, write_product
from scripts.ci.prepared_dependencies import prepared_environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--component", required=True, nargs="+")
    parser.add_argument("--configure", required=True)
    parser.add_argument("--configuration", required=True)
    parser.add_argument('--export-products', action='store_true')
    args = parser.parse_args()
    if args.export_products and len(args.component) != 1:
        raise ValueError('A compiled product must represent exactly one component')
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
    environment = os.environ.copy()
    environment['ATLAS_BUILD_CONFIGURATION'] = args.configuration
    with prepared_environment(environment, config, args.component) as environment:
        execute_build(args, config, targets, labels, environment)


def execute_build(args, config, targets, labels, environment):
    build = Path("build") / args.configure
    if environment.get('ATLAS_CI_ARTIFACTS') == '1':
        plan = plan_components(config, set(args.component))
        parents = sorted({name for component in args.component
                          for name in plan['prerequisites'][component]})
        validate_products(environment.get('ATLAS_OWNED_ROOT', 'build/owned'), parents,
                          args.configure, args.configuration,
                          environment['GITHUB_SHA'], environment['GITHUB_RUN_ID'])
    subprocess.run(configure_command(args.configure, version=environment.get('ATLAS_BUILD_VERSION'),
                                      components=args.component, environment=environment),
                   check=True, env=environment)
    subprocess.run(
        ["cmake", "--build", str(build), "--config", args.configuration, "--target", *targets],
        check=True, env=environment,
    )
    expression = "^(" + "|".join(re.escape(label) for label in labels) + ")$"
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
    if args.export_products:
        component = args.component[0]
        destination = Path(environment.get('ATLAS_PRODUCTS_ROOT', 'build/products')) / component
        subprocess.run(['cmake', '--install', str(build), '--config', args.configuration,
                        '--prefix', str(destination.resolve())], check=True, env=environment)
        write_product(destination, component, args.configure, args.configuration,
                      environment['GITHUB_SHA'], environment['GITHUB_RUN_ID'])


if __name__ == "__main__":
    main()
