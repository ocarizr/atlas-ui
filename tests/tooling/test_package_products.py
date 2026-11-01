"""Merge validated same-revision Release artifacts without recompiling sources."""

import pytest

from scripts.ci.owned_products import write_product
from scripts.ci.package_products import merge_products


@pytest.fixture
def products(tmp_path):
    """Create compatible owned artifact trees in the isolated test directory."""
    roots = []
    for name in ("game", "editor", "tools"):
        root = tmp_path / name
        write_product(root, name, "windows-msvc", "Release", "abc", "100")
        (root / "bin").mkdir()
        (root / "bin" / f"{name}.exe").write_bytes(name.encode())
        (root / "LICENSE").write_text("same notices")
        roots.append(root)
    return roots


def test_merges_release_products_without_building_sources(products, tmp_path):
    """Verify merges release products without building sources."""
    destination = tmp_path / "package"
    merge_products(products, destination, "abc", "100")
    assert sorted(path.name for path in (destination / "bin").iterdir()) == [
        "editor.exe",
        "game.exe",
        "tools.exe",
    ]
    assert (destination / "LICENSE").read_text() == "same notices"


def test_conflicting_runtime_binaries_fail_instead_of_overwriting(products, tmp_path):
    """Verify conflicting runtime binaries fail instead of overwriting."""
    for index, root in enumerate(products):
        (root / "bin" / "shared.dll").write_bytes(bytes([index]))
    with pytest.raises(ValueError, match="Conflicting package product"):
        merge_products(products, tmp_path / "package", "abc", "100")


def test_wrong_revision_cannot_be_packaged(products, tmp_path):
    """Verify wrong revision cannot be packaged."""
    with pytest.raises(ValueError, match="Incompatible compiled component"):
        merge_products(products, tmp_path / "package", "other", "100")
