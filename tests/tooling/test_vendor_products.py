"""Fingerprint pinned vendor manifests and toolchain compatibility."""

from copy import deepcopy
from pathlib import Path

import pytest

from scripts.ci.vendor_products import fingerprint, manifest_for

ROOT = Path(__file__).resolve().parents[2]
TOOLCHAIN = {"msvc": "14.44", "sdk": "10.0.26100.0", "llvm": "19.1", "triplet": "x64-windows"}


def test_jobs_testing_manifest_has_no_grpc():
    """Verify jobs testing manifest has no grpc."""
    assert manifest_for(ROOT, "testing")["dependencies"] == ["gtest"]


@pytest.mark.parametrize("field", ["msvc", "sdk", "llvm", "triplet"])
def test_toolchain_changes_invalidate_vendor_product(field):
    """Verify toolchain changes invalidate vendor product."""
    changed = deepcopy(TOOLCHAIN)
    changed[field] += "-changed"
    assert fingerprint(ROOT, "testing", TOOLCHAIN) != fingerprint(ROOT, "testing", changed)


def test_content_group_does_not_include_unrelated_grpc_or_media():
    """Verify content group does not include unrelated grpc or media."""
    assert manifest_for(ROOT, "content")["dependencies"] == [
        "entt",
        "pugixml",
        "yaml-cpp",
        "spdlog",
    ]


def test_unknown_vendor_group_is_rejected():
    """Verify unknown vendor group is rejected."""
    with pytest.raises(ValueError, match="Unknown dependency group"):
        manifest_for(ROOT, "unknown")
