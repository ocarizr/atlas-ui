add_executable(atlas_localization_benchmark context_benchmark.cpp
    ../jobs/allocation_counter.cpp ../jobs/allocation_counter_check.cpp)
target_link_libraries(atlas_localization_benchmark PRIVATE atlas::localization)
atlas_configure_target(atlas_localization_benchmark)
if(BUILD_TESTING)
    add_test(NAME localization_benchmark_smoke COMMAND atlas_localization_benchmark --smoke)
    set_tests_properties(localization_benchmark_smoke PROPERTIES LABELS localization TIMEOUT 30)
endif()
