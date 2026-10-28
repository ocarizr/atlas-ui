"""Merge per-run LLVM profiles and report all selected test executables."""

import json
import subprocess


def collect_coverage(build, configuration, labels, profiles):
    files = sorted(profiles.glob("run-*.profraw"))
    if not files:
        raise ValueError("Tests produced no LLVM coverage profiles")
    merged = profiles / "merged.profdata"
    subprocess.run(
        ["llvm-profdata", "merge", "-sparse", *map(str, files), "-o", str(merged)], check=True
    )
    inventory = subprocess.run(
        ["ctest", "--test-dir", str(build), "-C", configuration, "-L", labels,
         "--show-only=json-v1"], check=True, capture_output=True, text=True,
    )
    executables = sorted({
        test["command"][0] for test in json.loads(inventory.stdout)["tests"]
        if test.get("command")
    })
    if not executables or any(not path.lower().endswith(".exe") for path in executables):
        raise ValueError("Coverage requires registered native test executables")
    command = ["llvm-cov", "report", executables[0], f"-instr-profile={merged}"]
    command.extend(f"-object={path}" for path in executables[1:])
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    (profiles / "report.txt").write_text(result.stdout, encoding="utf-8")
    print(result.stdout)
