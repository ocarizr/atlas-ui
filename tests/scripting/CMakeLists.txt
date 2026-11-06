add_executable(atlas_scripting_tests session_tests.cpp failure_tests.cpp observer_tests.cpp boundary_tests.cpp resource_tests.cpp diagnostics_tests.cpp)
target_include_directories(atlas_scripting_tests PRIVATE "${PROJECT_SOURCE_DIR}/benchmarks/mvvm")
target_link_libraries(atlas_scripting_tests PRIVATE atlas::scripting GTest::gtest_main)
atlas_configure_test(atlas_scripting_tests)
gtest_discover_tests(atlas_scripting_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS scripting TIMEOUT 30)
