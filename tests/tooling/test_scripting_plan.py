"""Keep Lua a pinned reusable dependency with consumer-directed component validation."""

import json
from pathlib import Path

from scripts.ci.config import read_yaml
from scripts.ci.impact import select_components
from scripts.ci.parallel_plan import plan_components
from scripts.ci.vendor_products import manifest_for


def test_scripting_core_requires_no_ui_renderer_or_jobs():
    """Native jobs communicate through the value mailbox instead of becoming a VM dependency."""
    configuration = read_yaml(".ci/components.yaml")
    plan = plan_components(configuration, {"scripting"})
    assert plan["components"] == ["diagnostics", "mvvm", "scripting"]
    assert plan["prerequisites"]["scripting"] == ["diagnostics", "mvvm"]
    assert select_components(configuration, ["libs/scripting/src/session.cpp"]) == {
        "scripting",
        "ui-scripting",
        "ui-content",
    }


def test_lua_cpp_feature_is_in_precompiled_content_manifest():
    """CI must restore the exception-safe C++ Lua library used by protected callbacks."""
    dependencies = manifest_for(Path.cwd(), "content")["dependencies"]
    lua = next(
        package
        for package in dependencies
        if isinstance(package, dict) and package["name"] == "lua"
    )
    assert lua["features"] == ["cpp"]
    overlay = json.loads(Path("dependencies/vcpkg-ports/lua/vcpkg.json").read_text())
    assert overlay["version"] == "5.5.1"
