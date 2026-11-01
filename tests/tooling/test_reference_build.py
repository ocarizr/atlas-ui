"""Validate public API discovery, pinned Doxygen execution and failed-build isolation."""

import shutil
from pathlib import Path

import pytest

from scripts.ci import build_reference

DATA = Path(__file__).parent / "data/doxygen"


@pytest.fixture
def reference_project(tmp_path, monkeypatch):
    """Provide one public library and deterministic Doxygen XML without native dependencies."""
    include = tmp_path / "libs/jobs/include"
    include.mkdir(parents=True)
    (include / "worker.hpp").write_text("// API", encoding="utf-8")
    (tmp_path / ".ci").mkdir()
    (tmp_path / ".ci/doxygen.yaml").write_text('version: "1.18.0"\n', encoding="utf-8")
    (tmp_path / ".ci/doxyfile").write_text("GENERATE_XML = YES\n", encoding="utf-8")
    monkeypatch.setattr(
        build_reference.subprocess, "check_output", lambda *args, **kwargs: "1.18.0 (build-hash)\n"
    )
    return tmp_path


def test_complete_generation_uses_public_headers_and_emits_snapshot(reference_project, monkeypatch):
    """The vendor build suffix must not reject the pinned release version."""
    output = reference_project / "build/reference"

    def doxygen(command, **kwargs):
        """Supply the fixture XML and verify explicit public-only Doxygen inputs."""
        assert command == ["doxygen", "-"]
        assert '"libs/jobs/include/worker.hpp"' in kwargs["input"]
        shutil.copytree(DATA, output / "jobs/xml")

    monkeypatch.setattr(build_reference.subprocess, "run", doxygen)
    build_reference.generate(
        reference_project, output, "doxygen", "https://github.com/o/r", "a" * 40
    )
    assert (output / "wiki/Home.md").is_file()
    assert (output / "wiki/manifest.yaml").is_file()


def test_wrong_doxygen_version_fails_before_creating_output(reference_project, monkeypatch):
    """Tool drift must not leave a publishable snapshot."""
    monkeypatch.setattr(
        build_reference.subprocess, "check_output", lambda *args, **kwargs: "1.17.0"
    )
    output = reference_project / "build/reference"
    with pytest.raises(ValueError, match="Expected Doxygen"):
        build_reference.generate(
            reference_project, output, "doxygen", "https://github.com/o/r", "a" * 40
        )
    assert not output.exists()
