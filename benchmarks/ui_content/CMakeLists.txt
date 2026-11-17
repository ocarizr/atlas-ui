add_executable(atlas_ui_prepared_benchmark prepared_benchmark.cpp)
target_link_libraries(atlas_ui_prepared_benchmark PRIVATE atlas::ui_content)
atlas_configure_target(atlas_ui_prepared_benchmark)

add_executable(atlas_ui_readiness_benchmark readiness_benchmark.cpp readiness_scenarios.cpp)
target_link_libraries(atlas_ui_readiness_benchmark PRIVATE atlas::ui_content)
target_compile_definitions(atlas_ui_readiness_benchmark PRIVATE
    ATLAS_READINESS_BUILD="$<CONFIG>"
    ATLAS_READINESS_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
atlas_configure_target(atlas_ui_readiness_benchmark)
if(BUILD_TESTING)
    add_test(NAME ui_prepared_benchmark_smoke COMMAND atlas_ui_prepared_benchmark --smoke)
    set_tests_properties(ui_prepared_benchmark_smoke PROPERTIES LABELS ui-content TIMEOUT 30)
    add_test(NAME ui_readiness_smoke COMMAND atlas_ui_readiness_benchmark --smoke)
    set_tests_properties(ui_readiness_smoke PROPERTIES LABELS ui-content TIMEOUT 30)
endif()
