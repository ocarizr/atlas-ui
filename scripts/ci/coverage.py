"""Merge per-run LLVM profiles and report all selected test executables."""

import json
import subprocess
from pathlib import Path


def coverage_executables(inventory, build):
    """Select owned native products, including children invoked through a protocol harness."""
    root = build.resolve()
    executables = set()
    for test in inventory["tests"]:
        owned = set()
        for argument in test.get("command", []):
            if not argument.lower().endswith(".exe"):
                continue
            path = Path(argument)
            path = path.resolve() if path.is_absolute() else (root / path).resolve()
            if path.is_relative_to(root):
                owned.add(str(path))
        if not owned:
            raise ValueError("Coverage requires registered owned native test executables")
        executables.update(owned)
    if not executables:
        raise ValueError("Coverage requires registered owned native test executables")
    return sorted(executables)


def collect_coverage(build, configuration, labels, profiles):
    """Merge raw LLVM profiles and write coverage for owned sources in the selected components."""
    files = sorted(profiles.glob("run-*.profraw"))
    if not files:
        raise ValueError("Tests produced no LLVM coverage profiles")
    merged = profiles / "merged.profdata"
    # Basenames plus cwd avoid Windows argument limits and weighted-list parsing of parent commas.
    (profiles / "inputs.txt").write_text(
        "\n".join(path.name for path in files) + "\n", encoding="utf-8"
    )
    subprocess.run(
        ["llvm-profdata", "merge", "-sparse", "--input-files=inputs.txt", "-o", "merged.profdata"],
        cwd=profiles,
        check=True,
    )
    inventory = subprocess.run(
        [
            "ctest",
            "--test-dir",
            str(build),
            "-C",
            configuration,
            "-L",
            labels,
            "--show-only=json-v1",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    executables = coverage_executables(json.loads(inventory.stdout), build)
    command = ["llvm-cov", "report", executables[0], f"-instr-profile={merged}"]
    command.extend(f"-object={path}" for path in executables[1:])
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    (profiles / "report.txt").write_text(result.stdout, encoding="utf-8")
    print(result.stdout)
