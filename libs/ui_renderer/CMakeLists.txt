add_library(atlas_ui_renderer STATIC src/paint_renderer.cpp src/paint_draws.cpp)
add_library(atlas::ui_renderer ALIAS atlas_ui_renderer)
target_include_directories(atlas_ui_renderer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(atlas_ui_renderer PUBLIC cxx_std_20)
target_link_libraries(atlas_ui_renderer PUBLIC atlas::ui atlas::renderer atlas::render_host)
atlas_configure_target(atlas_ui_renderer)
atlas_export_library(atlas_ui_renderer ui_renderer ui_renderer)
