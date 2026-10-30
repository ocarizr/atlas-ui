from pathlib import Path

from scripts.ci.config import read_yaml

ROOT = Path(__file__).resolve().parents[2]


def test_quality_does_not_compile_dependencies():
    workflow = read_yaml(ROOT / ".github/workflows/reusable_quality.yml")
    commands = [step.get("run", "") for step in workflow["jobs"]["quality"]["steps"]]
    assert not any("cmake --preset" in command for command in commands)


def test_dependency_timeout_leaves_time_to_save_partial_cache():
    workflow = read_yaml(ROOT / ".github/workflows/reusable_dependencies.yml")
    job = workflow["jobs"]["dependencies"]
    install = next(step for step in job["steps"] if step.get("id") == "install")
    assert job["timeout-minutes"] >= install["timeout-minutes"] + 20
    save = next(step for step in job["steps"] if "/save@" in step.get("uses", ""))
    assert "always()" in save["if"]


def test_pr_builds_wait_for_dependency_preparation():
    workflow = read_yaml(ROOT / ".github/workflows/pr.yml")
    assert "dependencies" in workflow["jobs"]["build"]["needs"]
    assert "dependencies" in workflow["jobs"]["required"]["needs"]


def test_cache_restores_previous_compatible_snapshots():
    action = read_yaml(ROOT / ".github/actions/setup_ci/action.yml")
    restores = [step for step in action["runs"]["steps"] if "/restore@" in step.get("uses", "")]
    assert len(restores) == 1
    assert restores[0]["with"]["restore-keys"].strip()
