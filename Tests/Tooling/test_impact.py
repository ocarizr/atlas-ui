import pytest

from scripts.ci.impact import build_matrix, select_components, validate_components


@pytest.fixture
def component_config():
    return {
        "components": {
            "shared": {"paths": ["Engine/UI/**"], "depends_on": []},
            "game": {"paths": ["Apps/AtlasGame/**"], "depends_on": ["shared"]},
            "editor": {"paths": ["Apps/AtlasEditor/**"], "depends_on": ["shared"]},
        },
        "global_paths": ["cmake/**"],
        "documentation_paths": ["docs/**"],
        "asset_paths": ["Assets/**"],
        "profiles": [{"configure": "windows-msvc", "configuration": "Debug"}],
    }


@pytest.mark.parametrize(
    "path, expected",
    [
        pytest.param("Apps/AtlasEditor/main.cpp", {"editor"}, id="editor-only"),
        pytest.param("Apps/AtlasGame/main.cpp", {"game"}, id="game-only"),
        pytest.param("Engine/UI/Layout.cpp", {"shared", "game", "editor"}, id="shared-ui"),
        pytest.param("cmake/Options.cmake", {"shared", "game", "editor"}, id="global-build"),
        pytest.param("docs/design.md", set(), id="documentation-only"),
        pytest.param("Assets/menu.xml", set(), id="assets-only"),
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
        build_matrix(component_config, {"shared"}, ["Engine/UI/Layout.cpp"])


def test_matrix_uses_named_targets_and_test_labels(component_config):
    component_config["components"]["editor"].update(
        build_targets=["atlas_editor", "atlas_editor_tests"], test_labels=["editor"]
    )
    assert build_matrix(component_config, {"editor"}, []) == {
        "include": [{"component": "editor", "configure": "windows-msvc", "configuration": "Debug"}]
    }
