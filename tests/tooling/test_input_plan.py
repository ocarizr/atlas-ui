"""Resolve only the prebuilt libraries required by one component."""

import json
import sys

import pytest

from scripts.ci import verify_vendor_inputs


@pytest.mark.parametrize(
    "component,groups,pattern",
    [
        pytest.param("jobs", ["testing"], "@()", id="independent-library"),
        pytest.param("streaming", ["testing", "media"], "@(jobs)", id="single-prerequisite"),
        pytest.param(
            "editor",
            ["memory", "ipc", "content", "graphics"],
            "@(application|diagnostics|ipc|jobs|jobs-diagnostics|memory|renderer|window)",
            id="transitive-prerequisites",
        ),
    ],
)
def test_input_plan_restores_only_required_products(
    tmp_path, monkeypatch, component, groups, pattern
):
    """Verify input plan restores only required products."""
    destination = tmp_path / "outputs"
    monkeypatch.setenv("COMPONENT", component)
    monkeypatch.setenv("GITHUB_OUTPUT", str(destination))
    monkeypatch.delenv("KEYS", raising=False)
    monkeypatch.setattr(sys, "argv", ["verify_vendor_inputs", "--plan"])
    verify_vendor_inputs.main()
    outputs = dict(line.split("=", 1) for line in destination.read_text().splitlines())
    assert json.loads(outputs["groups"]) == groups
    assert outputs["owned-pattern"] == pattern
    assert outputs["has-owned"] == ("false" if component == "jobs" else "true")
