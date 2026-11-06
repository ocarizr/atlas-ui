add_executable(atlas_mvvm_tests runtime_tests.cpp)
target_link_libraries(atlas_mvvm_tests PRIVATE atlas::mvvm GTest::gtest_main)
atlas_configure_test(atlas_mvvm_tests)
gtest_discover_tests(atlas_mvvm_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS mvvm TIMEOUT 30)
target_sources(atlas_mvvm_tests PRIVATE dispatch_tests.cpp)
target_sources(atlas_mvvm_tests PRIVATE schema_tests.cpp)

target_sources(atlas_mvvm_tests PRIVATE resource_tests.cpp)
target_include_directories(atlas_mvvm_tests PRIVATE "${PROJECT_SOURCE_DIR}/benchmarks/mvvm")
target_sources(atlas_mvvm_tests PRIVATE relative_tests.cpp)
target_sources(atlas_mvvm_tests PRIVATE diagnostics_tests.cpp)
