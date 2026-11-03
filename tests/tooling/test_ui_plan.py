"""Keep UI state and commands independent from graphics, Lua, jobs and IPC."""

import pytest

from scripts.ci.config import read_yaml
from scripts.ci.impact import select_components
from scripts.ci.parallel_plan import plan_components


@pytest.fixture
def ui_configuration():
    """Load the actual component graph for each policy assertion."""
    return read_yaml(".ci/components.yaml")


@pytest.mark.parametrize(
    "path",
    [
        "libs/ui/src/commands.cpp",
        "tests/ui/command_merge_tests.cpp",
        "libs/ui/src/tree_application.cpp",
        "tests/ui/tree_transaction_tests.cpp",
    ],
    ids=["production-command-code", "command-tests", "tree-state", "tree-tests"],
)
def test_command_changes_do_not_select_unrelated_consumers(ui_configuration, path):
    """UI has no declared application consumer; its state changes select its own validation."""
    assert select_components(ui_configuration, [path]) == {"ui"}


def test_ui_uses_only_diagnostics_prerequisite(ui_configuration):
    """UI uses EnTT from content vendors without pulling a scheduler or graphical/IPC vendors."""
    plan = plan_components(ui_configuration, {"ui"})
    assert plan["components"] == ["diagnostics", "ui"]
    assert plan["prerequisites"]["ui"] == ["diagnostics"]
    assert plan["vendors"]["ui"] == ["content", "testing"]
    assert len(plan["matrices"]["ui"]["include"]) == len(ui_configuration["profiles"])
