"""Keep instrumented GoogleTest discovery independent from individual case limits."""

import re
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize(
    ("component", "case_timeout"),
    [("diagnostics", 20), ("renderer", 30)],
    ids=["diagnostics", "renderer"],
)
def test_instrumented_discovery_has_its_own_startup_budget(tmp_path, component, case_timeout):
    """Configure the real test target and inspect the effective PRE_TEST discovery invocation."""
    project = tmp_path / "CMakeLists.txt"
    project.write_text(
        "cmake_minimum_required(VERSION 3.25)\n"
        "project(discovery_policy LANGUAGES CXX)\n"
        "include(CTest)\n"
        "include(GoogleTest)\n"
        "add_library(instrumentation INTERFACE)\n"
        "add_library(atlas::instrumentation ALIAS instrumentation)\n"
        "add_library(diagnostics INTERFACE)\n"
        "add_library(atlas::diagnostics ALIAS diagnostics)\n"
        "add_library(renderer INTERFACE)\n"
        "add_library(atlas::renderer ALIAS renderer)\n"
        "add_library(gtest_main INTERFACE)\n"
        "add_library(GTest::gtest_main ALIAS gtest_main)\n"
        f'include("{(ROOT / "cmake/atlas_target_options.cmake").as_posix()}")\n'
        f'add_subdirectory("{(ROOT / "tests" / component).as_posix()}" {component})\n',
        encoding="utf-8",
    )
    build = tmp_path / "build"
    result = subprocess.run(
        ["cmake", "-S", str(tmp_path), "-B", str(build), "-G", "Ninja"],
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    includes = list((build / component).glob("*_include.cmake"))
    assert includes, "GoogleTest must generate a discovery invocation"
    discovery = "\n".join(path.read_text(encoding="utf-8") for path in includes)
    budget = re.search(r"TEST_DISCOVERY_TIMEOUT\s+\[==\[(\d+)\]==\]", discovery)
    assert budget, discovery
    assert int(budget[1]) == 30, (
        "Cold instrumented startup needs its own 30-second discovery budget"
    )
    assert f"LABELS;{component};TIMEOUT;{case_timeout}" in discovery, (
        f"Individual case limits must remain {case_timeout} seconds"
    )
