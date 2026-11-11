"""Keep UI dependency direction and consumer validation explicit."""

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
        "libs/ui/src/frame_work.cpp",
        "libs/ui/src/layout_compute.cpp",
        "tests/ui/data/layout_cases.xml",
        "benchmarks/ui/frame_execution_benchmark.cpp",
    ],
    ids=[
        "production-command-code",
        "command-tests",
        "tree-state",
        "tree-tests",
        "frame-work",
        "layout-work",
        "layout-fixtures",
        "phase-benchmark",
    ],
)
def test_command_changes_do_not_select_unrelated_consumers(ui_configuration, path):
    """Changes validate the adapter/apps without making graphics a core UI dependency."""
    assert select_components(ui_configuration, [path]) == {
        "ui",
        "ui-mvvm",
        "ui-scripting",
        "ui-content",
        "ui-renderer",
        "ui-images",
        "game",
        "editor",
    }


def test_ui_imports_the_shared_jobs_and_diagnostics_prerequisites(ui_configuration):
    """UI reuses the shared pool and content vendors without graphical/IPC dependencies."""
    plan = plan_components(ui_configuration, {"ui"})
    assert plan["components"] == ["diagnostics", "input", "jobs", "ui"]
    assert plan["prerequisites"]["ui"] == ["diagnostics", "input", "jobs"]
    assert plan["vendors"]["ui"] == ["content", "testing"]
    assert len(plan["matrices"]["ui"]["include"]) == len(ui_configuration["profiles"])


def test_authoring_restores_only_its_composition_inputs(ui_configuration):
    """Keep XML composition independent of apps, renderer, and IPC."""
    assert select_components(ui_configuration, ["libs/ui_content/src/instance.cpp"]) == {
        "ui-content"
    }
    plan = plan_components(ui_configuration, {"ui-content"})
    assert plan["prerequisites"]["ui-content"] == [
        "asset-catalog",
        "diagnostics",
        "input",
        "jobs",
        "mvvm",
        "mvvm-content",
        "scripting",
        "streaming",
        "ui",
        "ui-mvvm",
        "ui-scripting",
    ]
    assert plan["vendors"]["ui-content"] == ["content", "testing"]


@pytest.mark.parametrize(
    "path",
    [
        "libs/ui_images/src/image_cache.cpp",
        "tests/ui_images/cache_tests.cpp",
        "tests/platform/windows/ui_images/residency_tests.cpp",
    ],
    ids=["cache-source", "portable-tests", "native-tests"],
)
def test_image_changes_select_consumers_without_retesting_prerequisites(ui_configuration, path):
    """Image-only edits select host consumers, not unchanged jobs, streaming or core UI tests."""
    assert select_components(ui_configuration, [path]) == {"ui-images", "game", "editor"}
