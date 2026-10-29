"""Select changed components and transitive consumers for Windows validation."""

import argparse
import fnmatch
import json
import re
import subprocess
from pathlib import Path

from scripts.ci.config import read_yaml, write_outputs

SOURCE_SUFFIXES = {".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hxx", ".inl", ".ixx", ".proto"}


def matches(path, patterns):
    return any(fnmatch.fnmatchcase(path.replace("\\", "/"), pattern) for pattern in patterns)


def validate_components(config):
    components = config["components"]
    visited, active = set(), set()

    def visit(name):
        if name not in components:
            raise ValueError(f"Unknown component dependency: {name}")
        if name in active:
            raise ValueError(f"Component dependency cycle: {name}")
        if name in visited:
            return
        if not re.fullmatch(r"[a-z][a-z0-9-]*", name):
            raise ValueError(f"Invalid component name: {name}")
        active.add(name)
        for dependency in components[name].get("depends_on", []):
            visit(dependency)
        active.remove(name)
        visited.add(name)

    for name in components:
        visit(name)


def select_components(config, paths, full=False):
    validate_components(config)
    components = config["components"]
    selected = set(components) if full else set()
    for path in paths:
        owners = {name for name, item in components.items() if matches(path, item["paths"])}
        if (Path(path).suffix.lower() in SOURCE_SUFFIXES and not owners
                and not path.startswith(("ThirdParty/", "Tests/Support/"))):
            raise ValueError(f"Unmapped source file: {path}")
        if matches(path, config.get("global_paths", [])):
            selected.update(components)
            continue
        selected.update(owners)
        known_content = matches(path, config.get("asset_paths", []))
        known_docs = matches(path, config.get("documentation_paths", []))
        if not owners and not known_content and not known_docs:
            if Path(path).suffix.lower() in SOURCE_SUFFIXES:
                raise ValueError(f"Unmapped source file: {path}")
            selected.update(components)  # Unknown infrastructure is conservatively global.
    while True:
        consumers = {
            name for name, item in components.items()
            if selected.intersection(item.get("depends_on", []))
        }
        if consumers <= selected:
            return selected
        selected.update(consumers)


def build_matrix(config, selected, source_files):
    components_to_build = []
    for name in sorted(selected):
        item = config["components"][name]
        targets, labels = item.get("build_targets", []), item.get("test_labels", [])
        has_source = any(
            Path(path).suffix.lower() in SOURCE_SUFFIXES and matches(path, item["paths"])
            for path in source_files
        )
        if not targets:
            if has_source:
                raise ValueError(f"Component {name} has source but no build_targets")
            continue
        if not labels:
            raise ValueError(f"Component {name} needs test_labels")
        if any(not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.:+-]*", target) for target in targets):
            raise ValueError(f"Invalid build target for {name}")
        components_to_build.append(name)
    return {"include": [
        {"components": components_to_build, **profile} for profile in config["profiles"]
    ] if components_to_build else []}


def repository_files(root):
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=root, check=True, capture_output=True, text=True,
    )
    return [path for path in result.stdout.splitlines() if (root / path).is_file()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--github-output")
    args = parser.parse_args()
    root = Path.cwd()
    config = read_yaml(root / ".ci/components.yaml")
    if args.all:
        paths = []
    elif args.base:
        paths = subprocess.run(
            ["git", "diff", "--name-only", "--no-renames", f"{args.base}...{args.head}", "--"],
            check=True, capture_output=True, text=True,
        ).stdout.splitlines()
    else:
        parser.error("Provide --base for a PR or --all for full validation")
    selected = select_components(config, paths, args.all)
    files = repository_files(root)
    # Detect newly introduced source outside every registered context as well.
    select_components(config, [path for path in files if Path(path).suffix.lower() in SOURCE_SUFFIXES])
    matrix = build_matrix(config, selected, files)
    outputs = {
        "matrix": json.dumps(matrix, separators=(",", ":")),
        "has-build": bool(matrix["include"]),
        "configure": bool(selected) or args.all,
        "tooling": args.all or any(matches(path, config["global_paths"]) for path in paths),
    }
    write_outputs(outputs, args.github_output)
    print(json.dumps({"selected": sorted(selected), **outputs}, indent=2))


if __name__ == "__main__":
    main()
