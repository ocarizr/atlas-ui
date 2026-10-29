add_executable(atlas_streaming_tests MemoryReadTests.cpp FileReadTests.cpp LifecycleTests.cpp
    MediaPipelineTests.cpp ConcurrencyTests.cpp)
target_link_libraries(atlas_streaming_tests PRIVATE Atlas::Streaming Atlas::AudioDependencies
    GTest::gtest_main)
atlas_configure_test(atlas_streaming_tests)
gtest_discover_tests(atlas_streaming_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS streaming TIMEOUT 20)
