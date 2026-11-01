"""Validate the developer formatter entry point and independent CI checks."""

import sys
import sysconfig
from pathlib import Path

import pytest

from scripts.ci import format_sources
from scripts.ci.config import read_yaml

ROOT = Path(__file__).resolve().parents[2]


def test_fix_formats_braces_and_control_spacing_in_one_invocation(tmp_path, monkeypatch):
    """The advertised fix command must leave a new unbraced source ready for check mode."""
    (tmp_path / ".ci").mkdir()
    (tmp_path / ".ci/components.yaml").write_text(
        "components:\n  sample:\n    paths: [sample.cpp]\n", encoding="utf-8"
    )
    (tmp_path / ".clang-format").write_bytes((ROOT / ".clang-format").read_bytes())
    source = tmp_path / "sample.cpp"
    source.write_text(
        "/** @file sample.cpp */\n/// Execute the formatting example.\n"
        "void run(bool ready){if(ready) return; int value=1; (void)value;}\n",
        encoding="utf-8",
    )
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(format_sources, "repository_files", lambda root: ["sample.cpp"])
    monkeypatch.setattr(sys, "argv", ["format_sources", "--fix"])
    format_sources.main()
    result = source.read_text(encoding="utf-8")
    assert "if (ready)\n    {\n        return;\n    }\n\n" in result
    monkeypatch.setattr(sys, "argv", ["format_sources"])
    format_sources.main()
    assert source.read_text(encoding="utf-8") == result


def test_check_does_not_apply_fix_flags_to_either_language(monkeypatch):
    """CI must detect drift without rewriting the checkout or skipping Protobuf."""
    monkeypatch.setattr(format_sources, "formatter_executable", lambda override: "formatter")
    commands = format_sources.format_commands(["a.cpp", "schema.proto", "tool.py"], False)
    assert "--dry-run" in commands[0]
    assert "schema.proto" in commands[0]
    assert "--fix" not in commands[1]
    assert "--check" in commands[2]


def test_component_formatting_workflow_is_independent_and_required():
    """Each selected formatting context gets its own job and contributes to the merge gate."""
    validation = read_yaml(ROOT / ".github/workflows/reusable_validation.yml")
    assert validation["jobs"]["format"]["needs"] == "plan"
    assert validation["jobs"]["format"]["uses"].endswith("/reusable_format.yml")
    assert "format" in validation["jobs"]["required"]["needs"]
    gate = validation["jobs"]["required"]["steps"][0]["run"]
    assert "has-formatting" in gate and "format.result" in gate
    workflow = read_yaml(ROOT / ".github/workflows/reusable_format.yml")
    assert workflow["jobs"]["format"]["strategy"]["fail-fast"] is False


def test_formatter_version_cannot_silently_drift_from_ci(monkeypatch):
    """Reject a different toolchain formatter even if it is installed on the host."""
    monkeypatch.setattr(
        format_sources.subprocess,
        "check_output",
        lambda *args, **kwargs: "clang-format version 20.1.0",
    )
    with pytest.raises(ValueError, match="Expected clang-format 19.1.7"):
        format_sources.formatter_executable("other-formatter")


@pytest.mark.parametrize(
    ("python_path", "scripts_path"),
    [
        ("python/python.exe", "python/Scripts"),
        ("venv/Scripts/python.exe", "venv/Scripts"),
    ],
    ids=["global-python", "virtual-environment"],
)
def test_formatter_prefers_python_scripts_over_toolchain_path(
    monkeypatch, python_path, scripts_path
):
    """Use the pinned pip tool even when native setup prepends another LLVM version."""
    pinned = f"{scripts_path}/clang-format.exe"
    monkeypatch.setattr(sys, "executable", python_path)
    monkeypatch.setattr(sysconfig, "get_path", lambda name: scripts_path)
    monkeypatch.setattr(
        format_sources.shutil,
        "which",
        lambda name, path=None: (
            pinned if path and Path(path) == Path(scripts_path) else "toolchain/clang-format.exe"
        ),
    )
    monkeypatch.setattr(
        format_sources.subprocess,
        "check_output",
        lambda command, **kwargs: (
            "clang-format version 19.1.7" if command[0] == pinned else "clang-format version 20.1.8"
        ),
    )

    assert format_sources.formatter_executable() == pinned
