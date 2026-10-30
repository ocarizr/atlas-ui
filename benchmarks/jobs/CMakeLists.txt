add_executable(atlas_jobs_benchmark main.cpp options.cpp benchmark.cpp report.cpp
    phased_workload.cpp allocation_counter.cpp)
target_link_libraries(atlas_jobs_benchmark PRIVATE atlas::jobs)
atlas_configure_target(atlas_jobs_benchmark)
target_compile_definitions(atlas_jobs_benchmark PRIVATE
    ATLAS_BENCHMARK_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
    ATLAS_BENCHMARK_SYSTEM="${CMAKE_SYSTEM_NAME}"
    ATLAS_BENCHMARK_CONFIGURATION="$<CONFIG>")
if(BUILD_TESTING)
    add_test(NAME jobs-benchmark.smoke COMMAND atlas_jobs_benchmark --smoke)
    add_test(NAME jobs-benchmark.empty COMMAND atlas_jobs_benchmark --size 0 --rounds 1 --repetitions 1)
    add_test(NAME jobs-benchmark.invalid COMMAND atlas_jobs_benchmark --workers 0)
    add_test(NAME jobs-benchmark.help COMMAND atlas_jobs_benchmark --help)
    set_tests_properties(jobs-benchmark.smoke jobs-benchmark.empty jobs-benchmark.invalid jobs-benchmark.help
        PROPERTIES LABELS jobs-benchmark TIMEOUT 30)
    set_tests_properties(jobs-benchmark.invalid PROPERTIES WILL_FAIL TRUE)
endif()

install(TARGETS atlas_jobs_benchmark RUNTIME DESTINATION bin)
