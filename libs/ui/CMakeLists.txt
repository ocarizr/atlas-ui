add_library(atlas_ui STATIC src/commands.cpp src/command_sealing.cpp src/command_batch.cpp)
target_sources(atlas_ui PRIVATE src/element_tree.cpp src/tree_hierarchy.cpp src/tree_application.cpp)
target_sources(atlas_ui PRIVATE src/tree_snapshot.cpp src/frame_executor.cpp src/frame_work.cpp)
add_library(atlas::ui ALIAS atlas_ui)
target_sources(atlas_ui PRIVATE src/layout_validation.cpp src/tree_layout.cpp src/layout_snapshot.cpp
    src/layout_prepare.cpp src/layout_measure.cpp src/layout_arrange.cpp src/layout_engine.cpp src/layout_compute.cpp
    src/layout_phases.cpp)
target_compile_features(atlas_ui PUBLIC cxx_std_20)
target_include_directories(atlas_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(atlas_ui PUBLIC atlas::diagnostics atlas::jobs)
# EnTT is header-only implementation detail; installed consumers need only the compiled UI archive.
target_link_libraries(atlas_ui PRIVATE $<BUILD_INTERFACE:EnTT::EnTT>)
atlas_configure_target(atlas_ui)
atlas_export_library(atlas_ui ui ui)
