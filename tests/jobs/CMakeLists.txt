add_executable(atlas_jobs_tests execution_tests.cpp failure_tests.cpp lifecycle_tests.cpp
    graph_execution_tests.cpp graph_failure_tests.cpp graph_validation_tests.cpp
    graph_lifecycle_tests.cpp graph_concurrency_tests.cpp dispatch_tests.cpp child_task_tests.cpp
    task_lifetime_tests.cpp task_cycle_tests.cpp range_task_tests.cpp task_lifecycle_tests.cpp
    task_failure_tests.cpp context_dispatch_tests.cpp access_sanitizer_tests.cpp
    range_sanitizer_tests.cpp sanitizer_lifecycle_tests.cpp)
target_link_libraries(atlas_jobs_tests PRIVATE atlas::jobs GTest::gtest_main)
atlas_configure_test(atlas_jobs_tests)
include(GoogleTest)
gtest_discover_tests(atlas_jobs_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS jobs TIMEOUT 20)
