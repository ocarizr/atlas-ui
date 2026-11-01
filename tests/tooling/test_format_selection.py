"""Exercise independent formatting ownership and shared-policy invalidation."""

from pathlib import Path

import pytest

from scripts.ci.config import read_yaml
from scripts.ci.format_selection import formatting_files, formatting_plan

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture
def formatting_config():
    """Load the same ownership map used by component CI."""
    return read_yaml(ROOT / ".ci/components.yaml")


@pytest.mark.parametrize(
    ("changed", "expected"),
    [
        (["libs/jobs/src/task_handle.cpp"], ["jobs"]),
        (["apps/editor/main.cpp"], ["editor"]),
        (["protocols/session.proto"], ["ipc"]),
        (["tests/tooling/test_assets.py"], ["tooling"]),
        (["docs/04_job_system.md"], []),
    ],
    ids=["library-only", "editor-only", "protobuf", "python-tooling", "documentation-only"],
)
def test_formatting_checks_only_direct_owners(formatting_config, changed, expected):
    """Avoid formatting unrelated consumers when one project changes."""
    files = [
        "libs/jobs/src/task_handle.cpp",
        "apps/editor/main.cpp",
        "protocols/session.proto",
        "tests/tooling/test_assets.py",
    ]
    assert formatting_plan(formatting_config, changed, files) == expected


@pytest.mark.parametrize("policy", [".clang-format", "pyproject.toml", ".editorconfig"])
def test_shared_formatting_policy_rechecks_every_populated_context(formatting_config, policy):
    """A shared configuration change must not leave existing projects unchecked."""
    files = ["libs/jobs/src/task_handle.cpp", "scripts/ci/impact.py"]
    assert formatting_plan(formatting_config, [policy], files) == ["jobs", "tooling"]


def test_component_selection_preserves_proto_and_excludes_external_files(formatting_config):
    """Select source files rather than generated output or vendor trees."""
    files = ["protocols/session.proto", "third_party/vendor.cpp", "generated/schema.pb.h"]
    assert formatting_files(formatting_config, files, "ipc") == ["protocols/session.proto"]
    with pytest.raises(ValueError, match="Unknown formatting component"):
        formatting_files(formatting_config, files, "missing")


def test_unmapped_owned_python_source_cannot_silently_skip_formatting(formatting_config):
    """New source contexts must be registered even when they do not contain C++ targets."""
    with pytest.raises(ValueError, match="Unmapped formatting source: extra.py"):
        formatting_plan(formatting_config, ["extra.py"], ["extra.py"])
