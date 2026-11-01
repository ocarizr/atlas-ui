"""Validate parallel workflow scheduling and artifact retention contracts."""

from pathlib import Path

import pytest

from scripts.ci.config import read_yaml
from scripts.ci.impact import select_components

ROOT = Path(__file__).resolve().parents[2]


def test_streaming_source_change_does_not_mark_jobs_as_changed():
    """Change impact flows to consumers; link prerequisites are a separate scheduling concern."""
    config = read_yaml(ROOT / ".ci/components.yaml")
    selected = select_components(config, ["libs/streaming/src/file_pump.cpp"])
    assert "streaming" in selected
    assert "jobs" not in selected


@pytest.mark.parametrize("group", sorted(read_yaml(ROOT / ".ci/dependencies.yaml")["groups"]))
def test_each_vendor_product_is_restored_before_identity_validation(group):
    """A successful producer is insufficient unless the shared consumer action downloads its product."""
    steps = read_yaml(ROOT / ".github/actions/restore_inputs/action.yml")["runs"]["steps"]
    restore = [
        step
        for step in steps
        if step.get("with", {}).get("name", "").startswith(f"vendor-{group}-")
    ]
    assert len(restore) == 1, f"Missing or duplicated vendor restore: {group}"
    step = restore[0]
    assert step["with"]["path"] == f"build/vendor/{group}"
    assert f"['vendor-{group}'].outputs.run-id" in step["with"]["run-id"]
    assert f"'{group}'" in step["if"]
    verify = next(
        index
        for index, item in enumerate(steps)
        if item.get("name") == "Verify restored dependency identities"
    )
    assert steps.index(step) < verify


@pytest.mark.parametrize(
    "component,item",
    [
        (name, item)
        for name, item in read_yaml(ROOT / ".ci/components.yaml")["components"].items()
        if item.get("build_targets")
    ],
    ids=lambda value: value if isinstance(value, str) else "inputs",
)
def test_component_waits_for_all_declared_input_producers(component, item):
    """Do not allow restore to start before every required vendor and owned producer succeeds."""
    job = read_yaml(ROOT / ".github/workflows/reusable_validation.yml")["jobs"][component]
    expected = {
        "plan",
        *item.get("build_requires", []),
        *(f"vendor-{group}" for group in item.get("vendors", [])),
    }
    assert expected <= set(job["needs"])
    assert "success()" in job["if"]
    assert "always()" not in job["if"]


def test_library_jobs_do_not_wait_for_grpc_or_quality():
    """Verify library jobs do not wait for grpc or quality."""
    workflow = read_yaml(ROOT / ".github/workflows/reusable_validation.yml")
    needs = workflow["jobs"]["jobs"]["needs"]
    assert "vendor-testing" in needs
    assert "vendor-ipc" not in needs
    assert "quality" not in needs


def test_streaming_and_native_checks_can_run_without_ipc():
    """Verify streaming and native checks can run without ipc."""
    jobs = read_yaml(ROOT / ".github/workflows/reusable_validation.yml")["jobs"]
    for name in ("streaming", "windows", "dependencies"):
        assert "vendor-ipc" not in jobs[name]["needs"]
        assert "ipc" not in jobs[name]["needs"]


def test_component_outputs_are_current_run_profile_artifacts():
    """Verify component outputs are current run profile artifacts."""
    workflow = read_yaml(ROOT / ".github/workflows/reusable_build.yml")
    steps = workflow["jobs"]["component"]["steps"]
    uploads = [step for step in steps if "upload-artifact@" in step.get("uses", "")]
    assert any(
        "owned-" in step["with"]["name"] and "matrix.configure" in step["with"]["name"]
        for step in uploads
    )


def test_only_third_party_products_can_be_looked_up_between_runs():
    """Verify only third party products can be looked up between runs."""
    workflow = read_yaml(ROOT / ".github/workflows/reusable_dependencies.yml")
    steps = workflow["jobs"]["dependencies"]["steps"]
    lookup = next(step for step in steps if step.get("id") == "lookup")
    assert "scripts.ci.vendor_artifacts" in lookup["run"]
    upload = next(
        step
        for step in steps
        if "upload-artifact@" in step.get("uses", "")
        and step.get("name") == "Store compiled dependency product"
    )
    assert upload["with"]["retention-days"] == 14
