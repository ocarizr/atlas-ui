add_executable(atlas_memory_integration_tests consumer_tests.cpp)
target_link_libraries(atlas_memory_integration_tests PRIVATE
    atlas::memory atlas::jobs atlas::streaming GTest::gtest_main)
target_include_directories(atlas_memory_integration_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/streaming)
atlas_configure_test(atlas_memory_integration_tests)
gtest_discover_tests(atlas_memory_integration_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS memory-integration TIMEOUT 20)
