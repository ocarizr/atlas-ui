add_executable(atlas_ui_material_benchmark material_benchmark.cpp material_workload.cpp)
target_link_libraries(atlas_ui_material_benchmark PRIVATE atlas::ui_renderer)
target_compile_definitions(atlas_ui_material_benchmark PRIVATE ATLAS_MATERIAL_BUILD="$<CONFIG>"
    ATLAS_MATERIAL_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
atlas_configure_target(atlas_ui_material_benchmark)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    add_custom_command(TARGET atlas_ui_material_benchmark POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Microsoft::DXIL>" "$<TARGET_FILE_DIR:atlas_ui_material_benchmark>")
    if(BUILD_TESTING)
        add_test(NAME ui_material_benchmark_smoke COMMAND atlas_ui_material_benchmark --smoke)
        set_tests_properties(ui_material_benchmark_smoke PROPERTIES LABELS ui-renderer TIMEOUT 60)
    endif()
endif()
