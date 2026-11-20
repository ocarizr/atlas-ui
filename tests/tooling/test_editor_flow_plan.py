"""Preserve consumer-directed validation for portable graph authoring and its editor host."""

import pytest

from scripts.ci.config import read_yaml
from scripts.ci.impact import select_components
from scripts.ci.parallel_plan import plan_components


@pytest.fixture
def configuration():
    """Read the checked-in dependency contract independently for each scenario."""
    return read_yaml(".ci/components.yaml")


@pytest.mark.parametrize(
    "path",
    [
        "libs/editor_flow/src/persistence.cpp",
        "libs/editor_flow/include/atlas/editor_flow/document.hpp",
        "tests/editor_flow/session_tests.cpp",
        "tests/editor_flow/data/settings_flow.xml",
    ],
    ids=["production-code", "public-contract", "library-tests", "serialized-fixture"],
)
def test_graph_changes_validate_the_editor_without_retesting_prerequisites(configuration, path):
    """A graph-only change selects its consumer, leaving upstream native suites unselected."""
    assert select_components(configuration, [path]) == {"editor-flow", "editor"}


def test_editor_adapter_changes_do_not_retest_the_graph_library(configuration):
    """Application integration changes cannot reverse the shared-library dependency direction."""
    assert select_components(configuration, ["apps/editor/flow_file.cpp"]) == {"editor"}


def test_graph_products_include_content_inputs_without_native_graphics_or_ipc(configuration):
    """Compiling graph authoring needs UI-content products but no editor or graphics backend."""
    plan = plan_components(configuration, {"editor-flow"})
    assert plan["prerequisites"]["editor-flow"] == [
        "asset-catalog",
        "diagnostics",
        "input",
        "jobs",
        "mvvm",
        "mvvm-content",
        "scripting",
        "streaming",
        "ui",
        "ui-content",
        "ui-mvvm",
        "ui-scripting",
    ]
    assert set(plan["components"]) == {
        "asset-catalog",
        "diagnostics",
        "editor-flow",
        "input",
        "jobs",
        "mvvm",
        "mvvm-content",
        "scripting",
        "streaming",
        "ui",
        "ui-content",
        "ui-mvvm",
        "ui-scripting",
    }
    assert plan["vendors"]["editor-flow"] == ["content", "testing"]
    assert plan["groups"] == ["content", "media", "testing"]
    assert any(
        row["configure"] == "windows-portable" for row in plan["matrices"]["editor-flow"]["include"]
    )
