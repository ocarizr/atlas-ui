add_library(atlas_ui_images STATIC src/image_cache.cpp src/decoded_budget.cpp src/image_renderer.cpp src/image_residency.cpp)
add_library(atlas::ui_images ALIAS atlas_ui_images)
target_include_directories(atlas_ui_images PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(atlas_ui_images PUBLIC cxx_std_20)
target_link_libraries(atlas_ui_images PUBLIC atlas::ui_renderer atlas::renderer_content)
atlas_configure_target(atlas_ui_images)
atlas_export_library(atlas_ui_images ui_images ui_images)
