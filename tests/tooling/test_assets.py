"""Validate asset inventories, dependencies and XML failure paths."""

import pytest

from scripts.ci.validate_assets import validate_assets


@pytest.fixture
def asset_root(tmp_path):
    """Create an isolated asset tree for one inventory-validation case."""
    (tmp_path / "assets").mkdir()
    return tmp_path


@pytest.fixture
def write_inventory(asset_root):
    """Provide a per-test writer for asset inventories inside the temporary tree."""

    def write(assets):
        """Write the requested inventory document into the isolated fixture tree."""
        (asset_root / "assets/manifest.xml").write_text(
            f'<Assets schemaVersion="1">{assets}</Assets>', encoding="utf-8"
        )

    return write


def test_empty_foundation_needs_no_inventory(asset_root):
    """Verify empty foundation needs no inventory."""
    assert validate_assets(asset_root) == 0


def test_valid_inventory_maps_existing_assets(asset_root, write_inventory):
    """Verify valid inventory maps existing assets."""
    (asset_root / "assets/image.png").write_bytes(b"image fixture")
    write_inventory('<Asset id="image" type="texture" path="image.png"/>')
    assert validate_assets(asset_root) == 1


@pytest.mark.parametrize(
    "content, message",
    [
        pytest.param(
            '<Asset id="image" type="texture" path="missing.png"/>',
            "Asset path is missing",
            id="missing-file",
        ),
        pytest.param(
            '<Asset id="image" type="texture" path="../outside.png"/>',
            "stay within Assets",
            id="escaping-path",
        ),
        pytest.param(
            '<Asset id="x" type="ui" path="a.xml"/>' * 2, "IDs must be unique", id="duplicate-id"
        ),
        pytest.param(
            '<Asset id="x" type="ui" path="a.xml"><DependsOn id="missing"/></Asset>',
            "Invalid dependency",
            id="unknown-dependency",
        ),
    ],
)
def test_invalid_mappings_fail(asset_root, write_inventory, content, message):
    """Verify invalid mappings fail."""
    (asset_root / "assets/a.xml").write_text("<UI/>", encoding="utf-8")
    write_inventory(content)
    with pytest.raises(ValueError, match=message):
        validate_assets(asset_root)


def test_assets_require_inventory(asset_root):
    """Verify assets require inventory."""
    (asset_root / "assets/ui.xml").write_text("<UI/>", encoding="utf-8")
    with pytest.raises(ValueError, match="manifest"):
        validate_assets(asset_root)


@pytest.mark.parametrize(
    "content, message",
    [
        pytest.param("<UI>", "Invalid XML", id="malformed-xml"),
        pytest.param(
            '<!DOCTYPE UI [<!ENTITY x "value">]><UI>&x;</UI>',
            "DTD/entity",
            id="unsupported-doctype",
        ),
    ],
)
def test_invalid_xml_is_rejected(asset_root, write_inventory, content, message):
    """Verify invalid xml is rejected."""
    (asset_root / "assets/a.xml").write_text(content, encoding="utf-8")
    write_inventory('<Asset id="x" type="ui" path="a.xml"/>')
    with pytest.raises(ValueError, match=message):
        validate_assets(asset_root)
