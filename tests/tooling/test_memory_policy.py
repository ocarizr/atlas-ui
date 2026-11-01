"""Validate allocator selection and independent component scheduling without native compilation."""

import subprocess
from pathlib import Path

import pytest

from scripts.ci.config import read_yaml
from scripts.ci.parallel_plan import plan_components

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize(
    "backend,asan,expected",
    [
        ("auto", False, "mimalloc"),
        ("auto", True, "system"),
        ("system", False, "system"),
        ("system", True, "system"),
    ],
    ids=["normal-default", "asan-default", "system-comparison", "asan-system"],
)
def test_memory_backend_selection(tmp_path, backend, asan, expected):
    """ASan uses its supported system backend rather than silently bypassing heap instrumentation."""
    script = tmp_path / "memory.cmake"
    script.write_text(
        "cmake_minimum_required(VERSION 3.25)\n"
        f'set(ATLAS_MEMORY_BACKEND "{backend}")\n'
        f"set(ATLAS_ENABLE_ASAN {'ON' if asan else 'OFF'})\n"
        f'include("{(ROOT / "cmake/atlas_dependencies.cmake").as_posix()}")\n'
        'message(STATUS "Selected backend: ${atlas_memory_backend}")\n',
        encoding="utf-8",
    )
    result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert f"Selected backend: {expected}" in result.stdout


@pytest.mark.parametrize(
    "backend,asan,diagnostic",
    [
        ("unknown", False, "ATLAS_MEMORY_BACKEND must be"),
        ("mimalloc", True, "ASan profile requires the system"),
    ],
    ids=["invalid-backend", "unsupported-sanitizer-combination"],
)
def test_invalid_memory_policy_fails_explicitly(tmp_path, backend, asan, diagnostic):
    """Unsupported combinations must stop before finding/linking any third-party allocator."""
    script = tmp_path / "invalid.cmake"
    script.write_text(
        "cmake_minimum_required(VERSION 3.25)\n"
        f'set(ATLAS_MEMORY_BACKEND "{backend}")\n'
        f"set(ATLAS_ENABLE_ASAN {'ON' if asan else 'OFF'})\n"
        f'include("{(ROOT / "cmake/atlas_dependencies.cmake").as_posix()}")\n',
        encoding="utf-8",
    )
    result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
    assert result.returncode != 0
    assert diagnostic in result.stderr


def test_memory_producer_is_independent_of_jobs_streaming_and_grpc():
    """Only consumer integration checks need the job and streaming products."""
    configuration = read_yaml(ROOT / ".ci/components.yaml")
    producer = plan_components(configuration, {"memory"})
    assert producer["components"] == ["memory"]
    assert producer["groups"] == ["memory", "testing"]
    assert producer["prerequisites"]["memory"] == []
    consumers = plan_components(configuration, {"memory-integration"})
    assert consumers["prerequisites"]["memory-integration"] == ["jobs", "memory", "streaming"]
    assert "ipc" not in consumers["groups"]
