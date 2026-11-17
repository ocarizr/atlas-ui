add_library(atlas_ui_content STATIC src/xml_document.cpp src/xml_fields.cpp src/xml_bindings.cpp
    src/xml_layout.cpp src/xml_element.cpp src/document_validation.cpp src/storage.cpp
    src/model_sources.cpp src/tree_build.cpp src/routes.cpp src/instance_build.cpp src/instance.cpp
    src/animation.cpp src/animation_values.cpp src/animation_players.cpp
    src/animation_clock.cpp src/animation_composition.cpp src/animation_states.cpp
    src/material_document.cpp src/material_sources.cpp src/prepared_codec.cpp src/prepared_view.cpp
    src/flow_graph.cpp src/flow_xml.cpp src/flow_loader.cpp src/flow_navigation.cpp
    src/flow_manager.cpp src/flow_publication.cpp src/prepared_flow.cpp src/prepared_flow_codec.cpp)
add_library(atlas::ui_content ALIAS atlas_ui_content)
target_compile_features(atlas_ui_content PUBLIC cxx_std_20)
target_include_directories(atlas_ui_content PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(atlas_ui_content PUBLIC atlas::streaming atlas::ui_mvvm atlas::ui_scripting
    PRIVATE atlas::mvvm_content pugixml::pugixml)
atlas_configure_target(atlas_ui_content)
atlas_export_library(atlas_ui_content ui_content ui_content)
