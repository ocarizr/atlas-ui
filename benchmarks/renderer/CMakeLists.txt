add_executable(atlas_renderer_benchmark recording_benchmark.cpp)
target_link_libraries(atlas_renderer_benchmark PRIVATE atlas::renderer_jobs)
atlas_configure_target(atlas_renderer_benchmark)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    add_custom_command(TARGET atlas_renderer_benchmark POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Microsoft::DXIL>" "$<TARGET_FILE_DIR:atlas_renderer_benchmark>")
endif()
