"""Validate XML content and the mapped asset inventory."""

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath


def read_xml(path):
    content = path.read_bytes()
    # XML application data does not use DTDs or entity declarations.
    text = content.decode("utf-8-sig")
    if "<!DOCTYPE" in text.upper() or "<!ENTITY" in text.upper():
        raise ValueError(f"DTD/entity declarations are not supported: {path}")
    try:
        return ET.fromstring(content)
    except ET.ParseError as error:
        raise ValueError(f"Invalid XML in {path}: {error}") from error


def validate_assets(root):
    asset_root = (Path(root) / "assets").resolve()
    if not asset_root.exists():
        return 0
    files = [path for path in asset_root.rglob("*") if path.is_file()]
    if not files:
        return 0
    manifest = asset_root / "manifest.xml"
    if not manifest.is_file():
        raise ValueError("assets/manifest.xml is required when assets exist")
    for path in files:
        if path.suffix.lower() == ".xml":
            read_xml(path)
    document = read_xml(manifest)
    if document.tag != "Assets" or document.attrib.get("schemaVersion") != "1":
        raise ValueError("Expected <Assets schemaVersion=\"1\"> inventory")
    ids, mapped_paths, dependencies = set(), set(), []
    for asset in document:
        if asset.tag != "Asset":
            raise ValueError(f"Unknown inventory element: {asset.tag}")
        identity, kind, relative = (asset.get(key, "").strip() for key in ("id", "type", "path"))
        if not identity or not kind or not relative or identity in ids:
            raise ValueError("Asset id/type/path are required; IDs must be unique")
        portable = PurePosixPath(relative)
        if "\\" in relative or ":" in relative or portable.is_absolute() or ".." in portable.parts:
            raise ValueError(f"Asset path must stay within Assets: {relative}")
        path = (asset_root / relative).resolve()
        if not path.is_relative_to(asset_root) or not path.is_file() or path == manifest:
            raise ValueError(f"Asset path is missing or invalid: {relative}")
        normalized = path.relative_to(asset_root).as_posix().casefold()
        if normalized in mapped_paths:
            raise ValueError(f"Duplicate asset path: {relative}")
        ids.add(identity)
        mapped_paths.add(normalized)
        for dependency in asset:
            if dependency.tag != "DependsOn" or not dependency.get("id"):
                raise ValueError(f"Invalid dependency for asset {identity}")
            dependencies.append((identity, dependency.get("id")))
    for identity, dependency in dependencies:
        if dependency not in ids or dependency == identity:
            raise ValueError(f"Invalid dependency {identity} -> {dependency}")
    unmapped = {
        path.relative_to(asset_root).as_posix().casefold()
        for path in files if path != manifest
    } - mapped_paths
    if unmapped:
        raise ValueError(f"Unmapped asset files: {sorted(unmapped)}")
    return len(ids)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    count = validate_assets(args.root)
    print(f"Validated {count} mapped assets; no inventory is required for an empty foundation.")


if __name__ == "__main__":
    main()
