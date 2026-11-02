add_executable(atlas_renderer_readiness_tests)
target_link_libraries(atlas_renderer_readiness_tests PRIVATE atlas::renderer atlas::render_host GTest::gtest_main)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_renderer_readiness_tests PRIVATE ../platform/windows/renderer_readiness/threaded_surfaces_tests.cpp
        ../platform/windows/renderer_readiness/pressure_tests.cpp)
    target_include_directories(atlas_renderer_readiness_tests PRIVATE ${PROJECT_SOURCE_DIR}/libs/renderer/src
        ${PROJECT_SOURCE_DIR}/libs/window/src ${PROJECT_SOURCE_DIR}/libs/platform/windows/renderer
        ${PROJECT_SOURCE_DIR}/tests/platform/windows/renderer)
    target_compile_definitions(atlas_renderer_readiness_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED)
    target_link_libraries(atlas_renderer_readiness_tests PRIVATE GPUOpen::D3D12MemoryAllocator)
    add_custom_command(TARGET atlas_renderer_readiness_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Microsoft::DXIL>" "$<TARGET_FILE_DIR:atlas_renderer_readiness_tests>")
else()
    target_sources(atlas_renderer_readiness_tests PRIVATE unavailable_tests.cpp)
endif()
atlas_configure_test(atlas_renderer_readiness_tests)
gtest_discover_tests(atlas_renderer_readiness_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS renderer-readiness TIMEOUT 60)
