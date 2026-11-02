add_executable(atlas_renderer_jobs_tests executor_tests.cpp)
target_link_libraries(atlas_renderer_jobs_tests PRIVATE atlas::renderer_jobs GTest::gtest_main)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_include_directories(atlas_renderer_jobs_tests PRIVATE ${PROJECT_SOURCE_DIR}/libs/renderer/src
        ${PROJECT_SOURCE_DIR}/libs/window/src ${PROJECT_SOURCE_DIR}/libs/platform/windows/renderer
        ${PROJECT_SOURCE_DIR}/tests/platform/windows/renderer)
    target_link_libraries(atlas_renderer_jobs_tests PRIVATE GPUOpen::D3D12MemoryAllocator)
    target_compile_definitions(atlas_renderer_jobs_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED)
    target_sources(atlas_renderer_jobs_tests PRIVATE ../platform/windows/renderer_jobs/parallel_recording_tests.cpp)
    target_sources(atlas_renderer_jobs_tests PRIVATE ../platform/windows/renderer_jobs/coordinator_thread_tests.cpp)
    add_custom_command(TARGET atlas_renderer_jobs_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Microsoft::DXIL>" "$<TARGET_FILE_DIR:atlas_renderer_jobs_tests>")
endif()
atlas_configure_test(atlas_renderer_jobs_tests)
gtest_discover_tests(atlas_renderer_jobs_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS renderer-jobs TIMEOUT 60)
