add_library(atlas_ui STATIC src/commands.cpp src/command_sealing.cpp src/command_batch.cpp)

target_sources(atlas_ui PRIVATE src/input_session.cpp src/input_frames.cpp src/input_lifecycle.cpp src/input_routing.cpp src/input_shutdown.cpp)
target_sources(atlas_ui PRIVATE src/element_tree.cpp src/tree_hierarchy.cpp src/tree_application.cpp)
target_sources(atlas_ui PRIVATE src/tree_snapshot.cpp src/frame_executor.cpp src/frame_work.cpp)
add_library(atlas::ui ALIAS atlas_ui)
target_sources(atlas_ui PRIVATE src/image_fit.cpp)
target_sources(atlas_ui PRIVATE src/tree_paint.cpp src/paint_engine.cpp src/paint_prepare.cpp src/paint_jobs.cpp)
target_sources(atlas_ui PRIVATE src/paint_order.cpp)
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
target_link_libraries(atlas_ui PUBLIC atlas::diagnostics atlas::jobs)
# EnTT is header-only implementation detail; installed consumers need only the compiled UI archive.
target_link_libraries(atlas_ui PRIVATE $<BUILD_INTERFACE:EnTT::EnTT>)
atlas_configure_target(atlas_ui)
atlas_export_library(atlas_ui ui ui)
