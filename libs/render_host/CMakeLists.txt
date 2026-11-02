find_package(Threads REQUIRED)
add_library(atlas_render_host STATIC src/render_loop.cpp src/render_worker.cpp)
add_library(atlas::render_host ALIAS atlas_render_host)
target_compile_features(atlas_render_host PUBLIC cxx_std_20)
target_include_directories(atlas_render_host PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(atlas_render_host PUBLIC atlas::window atlas::diagnostics Threads::Threads)
atlas_configure_target(atlas_render_host)
atlas_export_library(atlas_render_host render_host render_host)
