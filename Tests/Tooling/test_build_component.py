import sys

import pytest

from scripts.ci import build_component


@pytest.fixture
def selected_build(monkeypatch):
    config = {
        "profiles": [{"configure": "windows-msvc", "configuration": "Debug"}],
        "components": {
            "jobs": {"build_targets": ["atlas_jobs", "atlas_jobs_tests"],
                     "test_labels": ["jobs"]},
            "ui": {"build_targets": ["atlas_jobs", "atlas_ui_tests"],
                   "test_labels": ["ui.behavior", "jobs"]},
        },
    }
    commands = []
    monkeypatch.setattr(build_component, "read_yaml", lambda _: config)
    monkeypatch.setattr(build_component.subprocess, "run", lambda command, **_: commands.append(command))
    monkeypatch.setattr(sys, "argv", [
        "build_component", "--component", "jobs", "ui",
        "--configure", "windows-msvc", "--configuration", "Debug",
    ])
    return commands


def test_selected_components_build_shared_targets_once(selected_build):
    build_component.main()
    command = next(item for item in selected_build if item[:2] == ["cmake", "--build"])
    assert command[command.index("--target") + 1:] == [
        "atlas_jobs", "atlas_jobs_tests", "atlas_ui_tests",
    ]


def test_ctest_label_union_uses_cmake_supported_groups(selected_build):
    build_component.main()
    command = next(item for item in selected_build if item[0] == "ctest")
    assert command[command.index("-L") + 1] == r"^(jobs|ui\.behavior)$"
    assert "--no-tests=error" in command
