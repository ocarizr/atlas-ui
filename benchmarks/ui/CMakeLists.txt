add_executable(atlas_ui_benchmark frame_execution_benchmark.cpp)
target_sources(atlas_ui_benchmark PRIVATE layout_benchmark.cpp layout_scenarios.cpp
    ../jobs/allocation_counter.cpp ../jobs/allocation_counter_check.cpp)
target_link_libraries(atlas_ui_benchmark PRIVATE atlas::ui)
atlas_configure_target(atlas_ui_benchmark)
add_executable(atlas_ui_paint_benchmark paint_benchmark.cpp)
target_sources(atlas_ui_paint_benchmark PRIVATE ../jobs/allocation_counter.cpp ../jobs/allocation_counter_check.cpp)
target_link_libraries(atlas_ui_paint_benchmark PRIVATE atlas::ui)
atlas_configure_target(atlas_ui_paint_benchmark)
target_compile_definitions(atlas_ui_paint_benchmark PRIVATE
    ATLAS_BENCHMARK_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
    ATLAS_BENCHMARK_SYSTEM="${CMAKE_SYSTEM_NAME}")
target_compile_definitions(atlas_ui_benchmark PRIVATE
    ATLAS_BENCHMARK_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
    ATLAS_BENCHMARK_SYSTEM="${CMAKE_SYSTEM_NAME}")
if(BUILD_TESTING)
    add_test(NAME ui_paint_benchmark_smoke COMMAND atlas_ui_paint_benchmark --smoke)
    set_tests_properties(ui_paint_benchmark_smoke PROPERTIES LABELS ui TIMEOUT 30)
    add_test(NAME ui_layout_benchmark_smoke COMMAND atlas_ui_benchmark --layout-smoke)
    set_tests_properties(ui_layout_benchmark_smoke PROPERTIES LABELS ui TIMEOUT 30)
endif()
