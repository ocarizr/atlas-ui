"""Preserve independent ViewModels, optional authoring and explicit UI adapters."""

from scripts.ci.config import read_yaml
from scripts.ci.impact import select_components
from scripts.ci.parallel_plan import plan_components


def test_mvvm_runtime_needs_no_ui_xml_renderer_or_jobs():
    """Runtime-only consumers retrieve the reusable core and diagnostics products."""
    configuration = read_yaml(".ci/components.yaml")
    plan = plan_components(configuration, {"mvvm"})
    assert plan["components"] == ["diagnostics", "mvvm"]
    assert plan["prerequisites"]["mvvm"] == ["diagnostics"]
    assert plan["groups"] == ["content", "testing"]


def test_mvvm_content_remains_independent_of_ui_and_graphics():
    """XML and codegen do not introduce graphical, IPC or executor dependencies."""
    configuration = read_yaml(".ci/components.yaml")
    plan = plan_components(configuration, {"mvvm-content"})
    assert plan["components"] == ["diagnostics", "mvvm", "mvvm-content"]
    assert plan["prerequisites"]["mvvm-content"] == ["diagnostics", "mvvm"]
    assert plan["groups"] == ["content", "testing"]


def test_mvvm_changes_validate_adapters_without_retesting_upstream_libraries():
    """Core changes validate their consumers without selecting unchanged UI/input/jobs suites."""
    configuration = read_yaml(".ci/components.yaml")
    assert select_components(configuration, ["libs/mvvm/src/dispatch.cpp"]) == {
        "mvvm",
        "mvvm-content",
        "ui-mvvm",
        "scripting",
        "ui-scripting",
    }
    assert select_components(configuration, ["libs/mvvm_content/src/xml_read.cpp"]) == {
        "mvvm-content"
    }
