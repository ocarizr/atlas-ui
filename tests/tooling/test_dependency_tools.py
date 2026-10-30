import sys

import pytest

from scripts.ci.configure import configure_command
from scripts.ci.dependency_cache import inventory
from scripts.ci.prepare_dependencies import native_progress, run_logged


def test_configuration_requires_cached_binaries_in_ci():
    command = configure_command("windows-asan", environment={
        "ATLAS_REQUIRE_BINARY_CACHE": "1", "ATLAS_VCPKG_INSTALLED_DIR": "build/shared deps",
    })
    assert command == ["cmake", "--preset", "windows-asan",
                       "-DVCPKG_INSTALLED_DIR=build/shared deps", "-DVCPKG_INSTALL_OPTIONS=--only-binarycaching"]


def test_local_configuration_keeps_normal_dependency_installation():
    assert configure_command("windows-msvc", environment={}) == ["cmake", "--preset", "windows-msvc"]


def test_artifact_consumers_cannot_be_redirected_to_legacy_installation(tmp_path):
    installed = tmp_path / 'installed'
    command = configure_command('windows-msvc', components=['jobs'], environment={
        'ATLAS_CI_ARTIFACTS': '1', 'ATLAS_CI_INSTALLED_DIR': str(installed),
        'ATLAS_CI_TOOLCHAIN': str(tmp_path / 'vcpkg.cmake'),
        'ATLAS_BUILD_CONFIGURATION': 'Debug', 'ATLAS_OWNED_ROOT': str(tmp_path / 'owned'),
        'ATLAS_VCPKG_INSTALLED_DIR': 'legacy', 'ATLAS_REQUIRE_BINARY_CACHE': '1',
    })
    assert [arg for arg in command if arg.startswith('-DVCPKG_INSTALLED_DIR=')] == [
        '-DVCPKG_INSTALLED_DIR=' + installed.resolve().as_posix()]
    assert '-DVCPKG_MANIFEST_INSTALL=OFF' in command
    assert '--fresh' in command
    assert '-DCMAKE_CONFIGURATION_TYPES=Debug' in command


@pytest.mark.parametrize("version", [
    pytest.param("", id="empty"), pytest.param("01.2.3", id="leading-zero"),
    pytest.param("v1.2.3", id="tag-prefix"), pytest.param("1.2", id="incomplete"),
    pytest.param("invalid", id="non-version"),
])
def test_package_configuration_rejects_invalid_versions(version):
    with pytest.raises(ValueError, match="Invalid package version"):
        configure_command("windows-msvc", version=version, environment={})


def test_cache_inventory_detects_new_archives_and_ignores_incomplete_files(tmp_path):
    original = inventory(tmp_path)
    assert original["has-archives"] is False
    (tmp_path / "partial.tmp").write_bytes(b"incomplete")
    assert inventory(tmp_path) == original
    (tmp_path / "package.zip").write_bytes(b"compiled archive")
    updated = inventory(tmp_path)
    assert updated["has-archives"] is True
    assert updated["fingerprint"] != original["fingerprint"]
    assert inventory(tmp_path) == updated


@pytest.mark.parametrize("code", [pytest.param(0, id="success"), pytest.param(7, id="build-failure")])
def test_dependency_runner_preserves_output_and_exit_status(tmp_path, capsys, code):
    log = tmp_path / "install.log"
    command = [sys.executable, "-c", f"print('Building package'); raise SystemExit({code})"]
    assert run_logged(command, log, tmp_path) == code
    assert "Building package" in log.read_text(encoding="utf-8")
    assert "Building package" in capsys.readouterr().out


def test_heartbeat_reports_native_build_progress_without_dumping_commands(tmp_path):
    log = tmp_path / "grpc/install-x64-windows-dbg-out.log"
    log.parent.mkdir()
    log.write_text("[125/1920] compiler with many arguments\n", encoding="utf-8")
    message = native_progress(tmp_path)
    assert "grpc" in message
    assert "125/1920" in message
    assert "many arguments" not in message
