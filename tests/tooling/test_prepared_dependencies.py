"""Merge prepared vendor installations and restore runtime search paths safely."""

import os
from pathlib import Path

import pytest

from scripts.ci.prepared_dependencies import merge_installed, prepared_environment


def test_combines_disjoint_vendor_packages_and_identical_helpers(tmp_path):
    """Verify combines disjoint vendor packages and identical helpers."""
    roots = [tmp_path / name for name in ("testing", "ipc")]
    for root in roots:
        (root / "x64-windows/lib").mkdir(parents=True)
        (root / "x64-windows/lib/common.lib").write_bytes(b"same")
        (root / "x64-windows/lib" / f"{root.name}.lib").write_bytes(root.name.encode())
    destination = tmp_path / "installed"
    merge_installed(roots, destination)
    assert sorted(path.name for path in (destination / "x64-windows/lib").iterdir()) == [
        "common.lib",
        "ipc.lib",
        "testing.lib",
    ]


def test_conflicting_dependency_outputs_fail_before_configuration(tmp_path):
    """Verify conflicting dependency outputs fail before configuration."""
    roots = [tmp_path / name for name in ("a", "b")]
    for root in roots:
        (root / "x64-windows").mkdir(parents=True)
        (root / "x64-windows/header.h").write_text(root.name)
    with pytest.raises(ValueError, match="Conflicting dependency product"):
        merge_installed(roots, tmp_path / "installed")


def test_merged_tree_does_not_share_vcpkg_manifest_status_files(tmp_path):
    """Verify merged tree does not share vcpkg manifest status files."""
    root = tmp_path / "source"
    (root / "vcpkg").mkdir(parents=True)
    (root / "vcpkg/status").write_text("per-group database")
    (root / "x64-windows/include").mkdir(parents=True)
    (root / "x64-windows/include/header.h").write_text("header")
    destination = tmp_path / "installed"
    merge_installed([root], destination)
    assert not (destination / "vcpkg").exists()


@pytest.mark.parametrize("configuration,first", [("Debug", "debug/bin"), ("Release", "bin")])
def test_runtime_search_order_matches_configuration_and_cleanup_runs_on_error(
    tmp_path, monkeypatch, configuration, first
):
    """Verify runtime search order matches configuration and cleanup runs on error."""
    monkeypatch.chdir(tmp_path)
    vendor = tmp_path / "vendor/testing"
    toolchain = vendor / "toolchain/scripts/buildsystems/vcpkg.cmake"
    toolchain.parent.mkdir(parents=True)
    toolchain.write_text("# fixture")
    header = vendor / "installed/x64-windows/include/header.h"
    header.parent.mkdir(parents=True)
    header.write_text("unchanged source")
    environment = {
        "ATLAS_CI_ARTIFACTS": "1",
        "ATLAS_VENDOR_ROOT": str(tmp_path / "vendor"),
        "ATLAS_BUILD_CONFIGURATION": configuration,
    }
    config = {"components": {"jobs": {"vendors": ["testing"]}}}
    with pytest.raises(RuntimeError, match="build failure"):
        with prepared_environment(environment, config, ["jobs"]) as prepared:
            installed = Path(prepared["ATLAS_CI_INSTALLED_DIR"])
            assert prepared["PATH"].split(os.pathsep)[0] == str(installed / "x64-windows" / first)
            raise RuntimeError("build failure")
    assert not installed.parent.exists()
    assert header.read_text() == "unchanged source"
