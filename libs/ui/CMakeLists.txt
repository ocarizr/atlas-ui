add_library(atlas_ui STATIC src/commands.cpp src/command_sealing.cpp src/command_batch.cpp)
target_sources(atlas_ui PRIVATE src/grid_layout.cpp src/tree_grid_slots.cpp)
target_sources(atlas_ui PRIVATE src/grid_placement.cpp)
target_sources(atlas_ui PRIVATE src/layout_grid_prepare.cpp src/layout_grid_measure.cpp src/layout_grid_arrange.cpp)
target_sources(atlas_ui PRIVATE src/virtual_source.cpp)
target_sources(atlas_ui PRIVATE src/virtual_tree_edit.cpp src/virtual_input_edit.cpp)
target_sources(atlas_ui PRIVATE src/virtual_list.cpp src/virtual_list_plan.cpp src/virtual_list_stage.cpp src/virtual_row.cpp src/virtual_navigation.cpp)

target_sources(atlas_ui PRIVATE src/input_session.cpp src/input_frames.cpp src/input_lifecycle.cpp src/input_routing.cpp src/input_shutdown.cpp)
target_sources(atlas_ui PRIVATE src/element_tree.cpp src/tree_hierarchy.cpp src/tree_application.cpp)
target_sources(atlas_ui PRIVATE src/tree_snapshot.cpp src/frame_executor.cpp src/frame_work.cpp)
target_sources(atlas_ui PRIVATE src/input_navigation_config.cpp src/input_navigation_select.cpp src/input_navigation_packets.cpp)
target_sources(atlas_ui PRIVATE src/input_accessibility.cpp src/input_accessibility_capture.cpp)
add_library(atlas::ui ALIAS atlas_ui)
target_sources(atlas_ui PRIVATE src/layout_scroll.cpp)
target_sources(atlas_ui PRIVATE src/scrollbar.cpp src/tree_scrollbar.cpp src/layout_scrollbar.cpp)
target_sources(atlas_ui PRIVATE src/input_scroll.cpp src/input_scroll_reveal.cpp src/input_scroll_pointer.cpp src/input_scroll_anchor.cpp)
target_sources(atlas_ui PRIVATE src/image_fit.cpp)
target_sources(atlas_ui PRIVATE src/tree_paint.cpp src/paint_engine.cpp src/paint_prepare.cpp src/paint_transform.cpp src/paint_jobs.cpp)
target_sources(atlas_ui PRIVATE src/paint_order.cpp)
target_sources(atlas_ui PRIVATE src/material.cpp)
target_sources(atlas_ui PRIVATE src/layout_partition.cpp src/layout_subtrees.cpp)
target_sources(atlas_ui PRIVATE src/boundary_validation.cpp src/tree_boundaries.cpp
    src/tree_boundary_lifetime.cpp src/layout_boundaries.cpp src/layout_boundary_order.cpp)
target_sources(atlas_ui PRIVATE src/stack_validation.cpp src/tree_stack_slots.cpp src/layout_order.cpp src/layout_stack.cpp src/layout_stack_axis.cpp src/layout_stack_weights.cpp src/layout_stack_shrink.cpp)
target_sources(atlas_ui PRIVATE src/canvas_validation.cpp src/tree_panels.cpp src/tree_canvas_slots.cpp src/tree_visual.cpp src/layout_canvas.cpp)
target_sources(atlas_ui PRIVATE src/layout_validation.cpp src/tree_layout.cpp src/layout_snapshot.cpp
    src/layout_prepare.cpp src/layout_measure.cpp src/layout_arrange.cpp src/layout_engine.cpp src/layout_compute.cpp
    src/layout_phases.cpp)
target_compile_features(atlas_ui PUBLIC cxx_std_20)
target_include_directories(atlas_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(atlas_ui PUBLIC atlas::input atlas::diagnostics atlas::jobs atlas::accessibility)
# EnTT is header-only implementation detail; installed consumers need only the compiled UI archive.
target_link_libraries(atlas_ui PRIVATE $<BUILD_INTERFACE:EnTT::EnTT>)
atlas_configure_target(atlas_ui)
atlas_export_library(atlas_ui ui ui)

target_sources(atlas_ui PRIVATE src/input_scroll_navigation.cpp)
