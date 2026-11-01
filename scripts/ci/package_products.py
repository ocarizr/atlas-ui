"""Package this run's validated Release applications without recompilation."""

import argparse
import hashlib
import os
import shutil
from pathlib import Path
from xml.etree import ElementTree

from scripts.ci.config import read_yaml
from scripts.ci.owned_products import validate_products
from scripts.ci.release_policy import version_from_tag


def checksum(path):
    """Hash one package input so identical duplicate paths can be merged safely."""
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def merge_products(roots, destination, revision, run):
    """Merge compatible component artifacts and reject conflicting binaries before packaging."""
    destination = Path(destination)
    for root in map(Path, roots):
        metadata_files = list((root / "share/atlas/ci").glob("*.yaml"))
        if len(metadata_files) != 1:
            raise ValueError("Each application product needs exactly one provenance record")
        component = read_yaml(metadata_files[0])["component"]
        validate_products(root, [component], "windows-msvc", "Release", revision, run)
        for source in sorted(root.rglob("*")):
            if not source.is_file() or "share/atlas" in source.relative_to(root).as_posix():
                continue
            relative = source.relative_to(root)
            target = destination / relative
            if target.exists():
                if target.stat().st_size != source.stat().st_size or checksum(target) != checksum(
                    source
                ):
                    raise ValueError(f"Conflicting package product: {relative}")
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)


def main():
    """Merge validated Release products and invoke packaging for the requested version."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    version = "0.1.0" if args.version == "development" else args.version
    version = version_from_tag("v" + version)
    roots = [Path("build/package_inputs") / name for name in ("game", "editor", "tools")]
    stage = Path("build/package_stage")
    merge_products(roots, stage, os.environ["GITHUB_SHA"], os.environ["GITHUB_RUN_ID"])
    manifest = stage / "package_manifest.xml"
    document = ElementTree.parse(manifest)
    document.getroot().set("version", version)
    document.write(manifest, encoding="utf-8", xml_declaration=True)
    archive = Path("build/packages") / f"atlas_ui-{version}-windows-x64"
    archive.parent.mkdir(parents=True, exist_ok=True)
    result = Path(shutil.make_archive(str(archive), "zip", stage))
    digest = checksum(result)
    result.with_suffix(".zip.sha256").write_text(f"{digest}  {result.name}\n", encoding="utf-8")


if __name__ == "__main__":
    main()
