add_executable(atlas_streaming_tests memory_read_tests.cpp file_read_tests.cpp lifecycle_tests.cpp
    media_pipeline_tests.cpp concurrency_tests.cpp buffer_budget_tests.cpp scheduling_tests.cpp
    buffer_failure_tests.cpp cancelled_copy_tests.cpp io_policy_tests.cpp file_pump_tests.cpp)
target_link_libraries(atlas_streaming_tests PRIVATE atlas::streaming atlas::audio_dependencies
    GTest::gtest_main)
atlas_configure_test(atlas_streaming_tests)
target_include_directories(atlas_streaming_tests PRIVATE ${PROJECT_SOURCE_DIR}/libs/streaming/src)
if(ATLAS_TARGET_PLATFORM STREQUAL "windows" AND ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_streaming_tests PRIVATE
        ../platform/windows/streaming/iocp_completion_tests.cpp
        ../platform/windows/streaming/iocp_cancellation_tests.cpp
        ../platform/windows/streaming/native_iocp_tests.cpp)
    target_include_directories(atlas_streaming_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/libs/platform/windows/streaming)
    target_compile_definitions(atlas_streaming_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
endif()
gtest_discover_tests(atlas_streaming_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS streaming TIMEOUT 20)
