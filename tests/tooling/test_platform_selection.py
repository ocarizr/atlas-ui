"""Validate platform selection without claiming support for unimplemented native adapters."""

import subprocess
from pathlib import Path

import pytest

from scripts.ci.config import read_yaml
from scripts.ci.impact import select_components

ROOT = Path(__file__).resolve().parents[2]


def test_storage_adapter_changes_select_streaming_without_graphics_checks():
    """The platform folder must not make a storage-only edit depend on DXC/graphics products."""
    config = read_yaml(ROOT / ".ci/components.yaml")
    selected = select_components(config, ["libs/platform/windows/streaming/file_reader.cpp"])
    assert "streaming" in selected
    assert "windows" not in selected


@pytest.mark.parametrize(
    "platform", ["linux", "macos", "android", "ios", "xbox", "playstation", "switch", "invalid"]
)
def test_unimplemented_platforms_fail_explicitly(tmp_path, platform):
    """Reserved names must not silently select a Windows implementation."""
    script = tmp_path / "platform.cmake"
    script.write_text(
        f'set(ATLAS_TARGET_PLATFORM "{platform}")\n'
        f'include("{(ROOT / "cmake/atlas_platform.cmake").as_posix()}")\n',
        encoding="utf-8",
    )
    result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
    assert result.returncode != 0
    assert "ATLAS_TARGET_PLATFORM" in result.stderr


def test_windows_selection_is_explicit_and_host_checked(tmp_path):
    """Selecting Windows for an incompatible target cannot configure native sources."""
    script = tmp_path / "platform.cmake"
    script.write_text(
        'set(ATLAS_TARGET_PLATFORM "windows")\nset(CMAKE_SYSTEM_NAME "Linux")\n'
        f'include("{(ROOT / "cmake/atlas_platform.cmake").as_posix()}")\n',
        encoding="utf-8",
    )
    result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
    assert result.returncode != 0
    assert "Windows target" in result.stderr
