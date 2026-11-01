"""Keep CMake, workflow edges and required checks consistent with component metadata."""

import re
from pathlib import Path

import pytest

from scripts.ci.config import read_yaml

ROOT = Path(__file__).resolve().parents[2]
COMPONENTS = read_yaml(ROOT / ".ci/components.yaml")["components"]


@pytest.mark.parametrize(
    "name", [name for name, item in COMPONENTS.items() if item["build_targets"]]
)
def test_cmake_prerequisites_match_the_pipeline_contract(name):
    """Verify cmake prerequisites match the pipeline contract."""
    text = (ROOT / "cmake/atlas_components.cmake").read_text(encoding="utf-8")
    match = re.search(rf"set\(atlas_requires_{name}\s+([^)]*)\)", text)
    actual = match.group(1).split() if match else []
    assert sorted(actual) == sorted(COMPONENTS[name]["build_requires"])


def test_dependency_products_include_the_vcpkg_root_marker():
    """Verify dependency products include the vcpkg root marker."""
    workflow = read_yaml(ROOT / ".github/workflows/reusable_dependencies.yml")
    upload = next(
        step
        for step in workflow["jobs"]["dependencies"]["steps"]
        if step.get("name") == "Store compiled dependency product"
    )
    assert upload["with"]["include-hidden-files"] is True


def test_packaging_does_not_compile_or_restore_binary_archives():
    """Verify packaging does not compile or restore binary archives."""
    workflow = read_yaml(ROOT / ".github/workflows/reusable_package.yml")
    steps = workflow["jobs"]["package"]["steps"]
    assert any("scripts.ci.package_products" in step.get("run", "") for step in steps)
    assert not any("cmake --build" in step.get("run", "") for step in steps)
    assert not any("/restore@" in step.get("uses", "") for step in steps)


@pytest.mark.parametrize(
    "name", [name for name, item in COMPONENTS.items() if item["build_targets"]]
)
def test_workflow_edges_match_declared_inputs(name):
    """Verify workflow edges match declared inputs."""
    workflow = read_yaml(ROOT / ".github/workflows/reusable_validation.yml")
    expected = {
        "plan",
        *COMPONENTS[name]["build_requires"],
        *(f"vendor-{group}" for group in COMPONENTS[name]["vendors"]),
    }
    assert set(workflow["jobs"][name]["needs"]) == expected


def test_tag_version_is_forwarded_to_each_producer():
    """Verify tag version is forwarded to each producer."""
    delivery = read_yaml(ROOT / ".github/workflows/delivery.yml")
    assert delivery["jobs"]["validation"]["with"]["version"] == "${{ needs.gate.outputs.version }}"
    validation = read_yaml(ROOT / ".github/workflows/reusable_validation.yml")
    for name, component in COMPONENTS.items():
        if component["build_targets"]:
            assert validation["jobs"][name]["with"]["version"] == "${{ inputs.version }}"


@pytest.mark.parametrize(
    "name", [name for name, item in COMPONENTS.items() if item["build_targets"]]
)
def test_required_gate_waits_for_every_compilable_component(name):
    """Verify required gate waits for every compilable component."""
    workflow = read_yaml(ROOT / ".github/workflows/reusable_validation.yml")
    assert name in workflow["jobs"]["required"]["needs"]
