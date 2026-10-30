import pytest

from scripts.ci.validate_assets import validate_assets


@pytest.fixture
def asset_root(tmp_path):
    (tmp_path / "assets").mkdir()
    return tmp_path


@pytest.fixture
def write_inventory(asset_root):
    def write(assets):
        (asset_root / "assets/manifest.xml").write_text(
            f'<Assets schemaVersion="1">{assets}</Assets>', encoding="utf-8"
        )
    return write


def test_empty_foundation_needs_no_inventory(asset_root):
    assert validate_assets(asset_root) == 0


def test_valid_inventory_maps_existing_assets(asset_root, write_inventory):
    (asset_root / "assets/image.png").write_bytes(b"image fixture")
    write_inventory('<Asset id="image" type="texture" path="image.png"/>')
    assert validate_assets(asset_root) == 1


@pytest.mark.parametrize(
    "content, message",
    [
        pytest.param('<Asset id="image" type="texture" path="missing.png"/>',
                     "Asset path is missing", id="missing-file"),
        pytest.param('<Asset id="image" type="texture" path="../outside.png"/>',
                     "stay within Assets", id="escaping-path"),
        pytest.param('<Asset id="x" type="ui" path="a.xml"/>' * 2,
                     "IDs must be unique", id="duplicate-id"),
        pytest.param('<Asset id="x" type="ui" path="a.xml"><DependsOn id="missing"/></Asset>',
                     "Invalid dependency", id="unknown-dependency"),
    ],
)
def test_invalid_mappings_fail(asset_root, write_inventory, content, message):
    (asset_root / "assets/a.xml").write_text("<UI/>", encoding="utf-8")
    write_inventory(content)
    with pytest.raises(ValueError, match=message):
        validate_assets(asset_root)


def test_assets_require_inventory(asset_root):
    (asset_root / "assets/ui.xml").write_text("<UI/>", encoding="utf-8")
    with pytest.raises(ValueError, match="manifest"):
        validate_assets(asset_root)


@pytest.mark.parametrize(
    "content, message",
    [
        pytest.param("<UI>", "Invalid XML", id="malformed-xml"),
        pytest.param('<!DOCTYPE UI [<!ENTITY x "value">]><UI>&x;</UI>',
                     "DTD/entity", id="unsupported-doctype"),
    ],
)
def test_invalid_xml_is_rejected(asset_root, write_inventory, content, message):
    (asset_root / "assets/a.xml").write_text(content, encoding="utf-8")
    write_inventory('<Asset id="x" type="ui" path="a.xml"/>')
    with pytest.raises(ValueError, match=message):
        validate_assets(asset_root)
