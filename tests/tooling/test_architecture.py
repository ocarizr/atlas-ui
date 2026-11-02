"""Validate portability boundaries and owned-source naming."""

from pathlib import Path

import pytest

from scripts.ci.architecture import validate_architecture


@pytest.mark.parametrize("header", ["Windows.h", "dxgi1_6.h", "d3d12sdklayers.h", "dxcapi.h"])
@pytest.mark.parametrize(
    "relative",
    [
        pytest.param("libs/jobs/include/atlas/jobs/job_system.hpp", id="portable-public-header"),
        pytest.param("tests/streaming/temporary_assets.hpp", id="portable-test-fixture"),
    ],
)
def test_platform_headers_cannot_leak_into_portable_layers(tmp_path, relative, header):
    """Verify platform headers cannot leak into portable layers."""
    source = tmp_path / relative
    source.parent.mkdir(parents=True)
    source.write_text(f"#include <{header}>\n", encoding="utf-8")
    with pytest.raises(ValueError, match="Platform header outside Windows boundary"):
        validate_architecture(tmp_path)


def test_windows_boundary_accepts_native_sdk_headers(tmp_path):
    """Verify windows boundary accepts native sdk headers."""
    source = tmp_path / "libs/platform/windows/graphics.cpp"
    source.parent.mkdir(parents=True)
    source.write_text("#include <d3d12.h>\n", encoding="utf-8")
    validate_architecture(tmp_path)


@pytest.mark.parametrize(
    "relative",
    [
        pytest.param("libs/Jobs/job_system.hpp", id="mixed-case-directory"),
        pytest.param("libs/jobs/MixedName.hpp", id="mixed-case-file"),
        pytest.param("libs/jobs/job-system.hpp", id="mixed-file-separator"),
    ],
)
def test_owned_paths_use_snake_case(tmp_path, relative):
    """Verify owned paths use snake case."""
    source = tmp_path / relative
    source.parent.mkdir(parents=True)
    source.write_text("// Portable library\n", encoding="utf-8")
    with pytest.raises(ValueError, match="Owned path must use snake_case"):
        validate_architecture(tmp_path)


def test_repository_follows_the_architecture_contract():
    """Verify repository follows the architecture contract."""
    validate_architecture(Path(__file__).resolve().parents[2])
