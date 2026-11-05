"""Keep physical input reusable and its affected consumers explicit."""

from scripts.ci.config import read_yaml
from scripts.ci.impact import select_components
from scripts.ci.parallel_plan import plan_components


def test_input_component_builds_without_ui_window_renderer_or_jobs():
    """Input action replay needs diagnostics and testing vendors, not an application."""
    configuration = read_yaml(".ci/components.yaml")
    plan = plan_components(configuration, {"input"})
    assert plan["components"] == ["diagnostics", "input"]
    assert plan["prerequisites"]["input"] == ["diagnostics"]
    assert plan["groups"] == ["content", "testing"]


def test_native_input_changes_validate_consumers_without_retesting_jobs():
    """Native adapter edits include UI/window consumers while unchanged jobs remain inputs."""
    configuration = read_yaml(".ci/components.yaml")
    selected = select_components(configuration, ["libs/platform/windows/input/win32_input.cpp"])
    assert {"input", "window", "ui", "renderer", "game", "editor"} <= selected
    assert "jobs" not in selected
    assert "diagnostics" not in selected
