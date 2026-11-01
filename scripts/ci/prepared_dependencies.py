"""Compose installed products for vcpkg's pinned CMake wrappers without installing packages."""

import hashlib
import os
import shutil
import tempfile
from contextlib import contextmanager
from pathlib import Path


def digest(path):
    """Hash an installed vendor file to distinguish identical helpers from conflicting outputs."""
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").digest()


def merge_installed(roots, destination):
    """Combine prepared vendor installations, rejecting conflicting files and manifest state."""
    destination = Path(destination)
    for root in map(Path, roots):
        for source in sorted((root / "x64-windows").rglob("*")):
            if not source.is_file():
                continue
            relative = source.relative_to(root)
            target = destination / relative
            if target.exists():
                if target.stat().st_size != source.stat().st_size or digest(target) != digest(
                    source
                ):
                    raise ValueError(f"Conflicting dependency product: {relative}")
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            try:
                os.link(source, target)
            except OSError:
                shutil.copy2(source, target)


@contextmanager
def prepared_environment(environment, config, components):
    """Temporarily prepend matching runtime directories and restore PATH even on failure."""
    if environment.get("ATLAS_CI_ARTIFACTS") != "1":
        yield environment
        return
    groups = sorted(
        {group for component in components for group in config["components"][component]["vendors"]}
    )
    vendor = Path(environment.get("ATLAS_VENDOR_ROOT", "build/vendor"))
    toolchain = vendor / groups[0] / "toolchain/scripts/buildsystems/vcpkg.cmake"
    if not toolchain.is_file():
        raise ValueError("Dependency product is missing its pinned CMake toolchain")
    Path("build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ci_dependencies_", dir="build") as temporary:
        installed = Path(temporary) / "installed"
        merge_installed([vendor / group / "installed" for group in groups], installed)
        result = environment.copy()
        result["ATLAS_CI_INSTALLED_DIR"] = str(installed.resolve())
        result["ATLAS_CI_TOOLCHAIN"] = str(toolchain.resolve())
        runtime = ["debug/bin", "bin"]
        if environment["ATLAS_BUILD_CONFIGURATION"] != "Debug":
            runtime.reverse()
        directories = [str(installed.resolve() / "x64-windows" / item) for item in runtime]
        result["PATH"] = os.pathsep.join([*directories, result.get("PATH", "")])
        yield result
