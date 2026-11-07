"""Keep coverage consolidation within Windows command-line limits without dropping profiles."""

import json
import subprocess
from types import SimpleNamespace

import pytest

from scripts.ci.coverage import collect_coverage, coverage_executables


def test_native_harness_uses_owned_child_instead_of_python(tmp_path):
    """A Python protocol driver contributes its instrumented native child to LLVM reports."""
    build = tmp_path / "build"
    host = build / "tools/Debug/host.exe"
    interpreter = tmp_path / "python/python.exe"
    inventory = {"tests": [{"command": [str(interpreter), "smoke.py", str(host)]}]}
    assert coverage_executables(inventory, build) == [str(host.resolve())]


def test_external_uninstrumented_command_cannot_be_reported_as_owned(tmp_path):
    """A foreign interpreter is not an instrumented Atlas test product."""
    inventory = {"tests": [{"command": [str(tmp_path / "python/python.exe"), "smoke.py"]}]}
    with pytest.raises(ValueError, match="owned native"):
        coverage_executables(inventory, tmp_path / "build")


@pytest.mark.parametrize(
    "directory", ["profiles", "profile data, with spaces"], ids=["plain", "spaces-and-comma"]
)
def test_many_profiles_merge_without_expanding_command_line(tmp_path, monkeypatch, directory):
    """A file-list merge retains every profile even when positional paths exceed Windows limits."""
    profiles = tmp_path / directory
    profiles.mkdir()
    names = [f"run-{index:05}.profraw" for index in range(500)]
    for name in names:
        (profiles / name).write_bytes(b"")
    invocations = []

    def run(command, **options):
        """Model the Windows limit and the registered native test inventory."""
        if len(subprocess.list2cmdline(command)) > 32767:
            raise OSError(206, "command line too long")
        invocations.append((command, options))
        if command[0] == "ctest":
            return SimpleNamespace(stdout=json.dumps({"tests": [{"command": ["renderer.exe"]}]}))
        return SimpleNamespace(stdout="coverage report\n")

    monkeypatch.setattr("scripts.ci.coverage.subprocess.run", run)
    collect_coverage(tmp_path / "build", "Debug", "renderer", profiles)
    merge, options = invocations[0]
    assert "--input-files=inputs.txt" in merge
    assert options["cwd"] == profiles
    assert (profiles / "inputs.txt").read_text(encoding="utf-8").splitlines() == names
    assert (profiles / "report.txt").read_text(encoding="utf-8") == "coverage report\n"


def test_missing_profiles_fail_before_invoking_tools(tmp_path, monkeypatch):
    """An empty capture never becomes a passing coverage report."""

    def run(*args, **options):
        """Reject unexpected native commands when no tests produced data."""
        pytest.fail("tools must not run without profiles")

    monkeypatch.setattr("scripts.ci.coverage.subprocess.run", run)
    with pytest.raises(ValueError, match="no LLVM coverage profiles"):
        collect_coverage(tmp_path / "build", "Debug", "renderer", tmp_path / "profiles")
