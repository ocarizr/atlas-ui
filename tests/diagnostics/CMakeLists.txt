add_executable(atlas_diagnostics_tests logging_tests.cpp logging_lifecycle_tests.cpp
    profiling_tests.cpp profiling_failure_tests.cpp settings_tests.cpp
    metrics_tests.cpp metrics_failure_tests.cpp metrics_reporting_tests.cpp instrumentation_tests.cpp)
target_link_libraries(atlas_diagnostics_tests PRIVATE atlas::diagnostics GTest::gtest_main)
atlas_configure_test(atlas_diagnostics_tests)
include(GoogleTest)
# Cold ASan startup on hosted runners can exceed GoogleTest's five-second default.
gtest_discover_tests(atlas_diagnostics_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS diagnostics TIMEOUT 20)
