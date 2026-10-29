"""Check documentation links, configuration syntax and workflow references."""

import json
import re
from pathlib import Path

from scripts.ci.architecture import validate_architecture

from scripts.ci.config import read_yaml
from scripts.ci.impact import repository_files, validate_components


def main():
    root = Path.cwd()
    validate_architecture(root)
    validate_components(read_yaml(root / ".ci/components.yaml"))
    errors = []
    for relative in repository_files(root):
        path = root / relative
        if path.suffix in {".yml", ".yaml"} and relative.startswith((".ci/", ".github/")):
            config = read_yaml(path)
            if relative.startswith(".github/workflows/"):
                if "on" not in config or "jobs" not in config:
                    errors.append(f"Missing workflow trigger/jobs: {relative}")
                for job in config.get("jobs", {}).values():
                    uses = job.get("uses", "")
                    if uses.startswith("./") and not (root / uses[2:]).is_file():
                        errors.append(f"Missing reusable workflow: {uses}")
        if path.name == "CMakePresets.json":
            json.loads(path.read_text(encoding="utf-8"))
        if path.suffix == ".md":
            text = path.read_text(encoding="utf-8")
            if text.count("```") % 2:
                errors.append(f"Unbalanced code fences: {relative}")
            for target in re.findall(r"\[[^\]]+\]\(([^)]+)\)", text):
                if "://" in target or target.startswith("#"):
                    continue
                target = target.split("#")[0]
                if not (path.parent / target).exists():
                    errors.append(f"Broken link in {relative}: {target}")
    if errors:
        raise ValueError("\n".join(errors))
    print("Repository links, configuration and local workflow references are valid.")


if __name__ == "__main__":
    main()
