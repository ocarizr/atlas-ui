"""Keep the shared benchmark allocation census in every consuming component's impact graph."""

from pathlib import Path

import pytest

from scripts.ci.config import read_yaml
from scripts.ci.impact import select_components


@pytest.mark.parametrize(
    "name",
    ["allocation_counter.cpp", "allocation_counter.hpp", "allocation_counter_check.cpp"],
    ids=["replacement-operators", "census-contract", "counter-self-check"],
)
def test_allocation_census_changes_validate_both_benchmark_consumers(name):
    """A shared measurement helper must trigger UI and jobs benchmark validation together."""
    root = Path(__file__).resolve().parents[2]
    config = read_yaml(root / ".ci/components.yaml")
    selected = select_components(config, [f"benchmarks/jobs/{name}"])
    assert {"ui", "jobs-benchmark"} <= selected
