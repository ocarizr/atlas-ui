add_library(atlas_editor_flow STATIC src/document.cpp src/document_xml.cpp src/xml_fields.cpp
    src/xml_records.cpp src/session.cpp src/compilation.cpp src/persistence.cpp
    src/workspace.cpp src/workspace_edits.cpp src/inspector.cpp src/conditions.cpp src/condition_xml.cpp
    src/canvas.cpp src/canvas_paint.cpp src/preview.cpp src/preview_queries.cpp src/preview_bindings.cpp)
add_library(atlas::editor_flow ALIAS atlas_editor_flow)
target_compile_features(atlas_editor_flow PUBLIC cxx_std_20)
target_include_directories(atlas_editor_flow PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(atlas_editor_flow PUBLIC atlas::ui_content PRIVATE pugixml::pugixml)
atlas_configure_target(atlas_editor_flow)
atlas_export_library(atlas_editor_flow editor_flow editor_flow)
