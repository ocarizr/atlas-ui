"""Keep persistent metadata independently buildable and available to streamer consumers."""

from scripts.ci.config import read_yaml
from scripts.ci.impact import select_components
from scripts.ci.parallel_plan import plan_components


def test_catalog_requires_no_storage_worker_or_renderer():
    """Authoring metadata can build with diagnostics and XML alone."""
    configuration = read_yaml(".ci/components.yaml")
    plan = plan_components(configuration, {"asset-catalog"})
    assert plan["components"] == ["asset-catalog", "diagnostics"]
    assert plan["prerequisites"]["asset-catalog"] == ["diagnostics"]
    assert plan["groups"] == ["content", "testing"]


def test_catalog_changes_select_typed_streamer_consumers():
    """A catalog change validates affected consumers with its compiled dependency product."""
    configuration = read_yaml(".ci/components.yaml")
    selected = select_components(configuration, ["libs/asset_catalog/src/catalog.cpp"])
    assert {"asset-catalog", "streaming", "renderer-content"} <= selected
    assert not {"mvvm", "scripting", "ipc"}.intersection(selected)
    plan = plan_components(configuration, {"streaming"})
    assert plan["prerequisites"]["streaming"] == ["asset-catalog", "diagnostics", "jobs"]
