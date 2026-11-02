add_library(atlas_renderer_jobs STATIC src/executor.cpp)
add_library(atlas::renderer_jobs ALIAS atlas_renderer_jobs)
target_include_directories(atlas_renderer_jobs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(atlas_renderer_jobs PUBLIC cxx_std_20)
target_link_libraries(atlas_renderer_jobs PUBLIC atlas::renderer atlas::jobs)
atlas_configure_target(atlas_renderer_jobs)
atlas_export_library(atlas_renderer_jobs renderer_jobs renderer_jobs)
