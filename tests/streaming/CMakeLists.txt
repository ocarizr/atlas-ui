add_executable(atlas_streaming_tests memory_read_tests.cpp file_read_tests.cpp lifecycle_tests.cpp
    media_pipeline_tests.cpp concurrency_tests.cpp buffer_budget_tests.cpp scheduling_tests.cpp
    buffer_failure_tests.cpp cancelled_copy_tests.cpp)
target_link_libraries(atlas_streaming_tests PRIVATE atlas::streaming atlas::audio_dependencies
    GTest::gtest_main)
atlas_configure_test(atlas_streaming_tests)
gtest_discover_tests(atlas_streaming_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS streaming TIMEOUT 20)
