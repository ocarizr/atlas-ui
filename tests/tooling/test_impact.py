import pytest

from scripts.ci.impact import build_matrix, select_components, validate_components


@pytest.fixture
def component_config():
    return {
        "components": {
            "shared": {"paths": ["libs/ui/**"], "depends_on": []},
            "game": {"paths": ["apps/game/**"], "depends_on": ["shared"]},
            "editor": {"paths": ["apps/editor/**"], "depends_on": ["shared"]},
        },
        "global_paths": ["cmake/**"],
        "documentation_paths": ["docs/**"],
        "asset_paths": ["assets/**"],
        "profiles": [{"configure": "windows-msvc", "configuration": "Debug"}],
    }


@pytest.mark.parametrize(
    "path, expected",
    [
        pytest.param("apps/editor/main.cpp", {"editor"}, id="editor-only"),
        pytest.param("apps/game/main.cpp", {"game"}, id="game-only"),
        pytest.param("libs/ui/Layout.cpp", {"shared", "game", "editor"}, id="shared-ui"),
        pytest.param("cmake/Options.cmake", {"shared", "game", "editor"}, id="global-build"),
        pytest.param("docs/design.md", set(), id="documentation-only"),
        pytest.param("assets/menu.xml", set(), id="assets-only"),
    ],
)
def test_changes_select_affected_consumers(component_config, path, expected):
    assert select_components(component_config, [path]) == expected


def test_unknown_source_fails(component_config):
    with pytest.raises(ValueError, match="Unmapped"):
        select_components(component_config, ["NewModule/source.cpp"])


def test_global_rule_cannot_hide_unmapped_source(component_config):
    component_config["global_paths"].append("NewModule/**")
    with pytest.raises(ValueError, match="Unmapped"):
        select_components(component_config, ["NewModule/source.cpp"])


@pytest.mark.parametrize(
    "dependency, message",
    [pytest.param("missing", "Unknown component", id="unknown-dependency"),
     pytest.param("editor", "dependency cycle", id="cyclic-dependency")],
)
def test_invalid_dependencies_fail(component_config, dependency, message):
    component_config["components"]["shared"]["depends_on"] = [dependency]
    with pytest.raises(ValueError, match=message):
        validate_components(component_config)


def test_empty_foundation_has_no_fake_build_matrix(component_config):
    assert build_matrix(component_config, {"shared"}, []) == {"include": []}


def test_unwired_cpp_source_fails(component_config):
    with pytest.raises(ValueError, match="build_targets"):
        build_matrix(component_config, {"shared"}, ["libs/ui/Layout.cpp"])


def test_matrix_uses_named_targets_and_test_labels(component_config):
    component_config["components"]["editor"].update(
        build_targets=["atlas_editor", "atlas_editor_tests"], test_labels=["editor"]
    )
    assert build_matrix(component_config, {"editor"}, []) == {
        "include": [{"components": ["editor"], "configure": "windows-msvc", "configuration": "Debug"}]
    }


def test_shared_consumers_share_one_build_per_profile(component_config):
    for name in ("game", "editor"):
        component_config["components"][name].update(
            build_targets=[f"atlas_{name}"], test_labels=[name]
        )
    assert build_matrix(component_config, {"game", "editor"}, []) == {
        "include": [{"components": ["editor", "game"],
                     "configure": "windows-msvc", "configuration": "Debug"}]
    }


def test_portable_profile_omits_native_components(component_config):
    for name in ("game", "editor"):
        component_config["components"][name].update(
            build_targets=[f"atlas_{name}"], test_labels=[name]
        )
    component_config["profiles"].append({
        "configure": "windows-portable", "configuration": "Debug", "exclude_components": ["editor"],
    })
    matrix = build_matrix(component_config, {"game", "editor"}, [])["include"]
    assert matrix[0]["components"] == ["editor", "game"]
    assert matrix[1] == {
        "components": ["game"], "configure": "windows-portable", "configuration": "Debug",
    }


def test_native_only_changes_do_not_start_an_empty_portable_build(component_config):
    component_config["components"]["editor"].update(
        build_targets=["atlas_editor"], test_labels=["editor"]
    )
    component_config["profiles"] = [{
        "configure": "windows-portable", "configuration": "Debug", "exclude_components": ["editor"],
    }]
    assert build_matrix(component_config, {"editor"}, []) == {"include": []}
