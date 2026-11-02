add_executable(atlas_renderer_content_tests image_contract_tests.cpp)
target_sources(atlas_renderer_content_tests PRIVATE loader_tests.cpp)
target_sources(atlas_renderer_content_tests PRIVATE diagnostics_tests.cpp)
target_link_libraries(atlas_renderer_content_tests PRIVATE atlas::renderer_content GTest::gtest_main)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    file(SIZE "${PROJECT_SOURCE_DIR}/assets/renderer/quad.hlsl" atlas_content_shader_size)
    target_sources(atlas_renderer_content_tests PRIVATE ../platform/windows/renderer_content/content_tests.cpp)
    target_include_directories(atlas_renderer_content_tests PRIVATE ${PROJECT_SOURCE_DIR}/libs/renderer/src
        ${PROJECT_SOURCE_DIR}/libs/window/src ${PROJECT_SOURCE_DIR}/libs/platform/windows/renderer
        ${PROJECT_SOURCE_DIR}/tests/platform/windows/renderer)
    target_compile_definitions(atlas_renderer_content_tests PRIVATE ATLAS_CONTENT_SHADER_SIZE=${atlas_content_shader_size} ATLAS_CONTENT_ASSETS="${PROJECT_SOURCE_DIR}/assets/renderer"
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED)
    target_link_libraries(atlas_renderer_content_tests PRIVATE GPUOpen::D3D12MemoryAllocator)
    add_custom_command(TARGET atlas_renderer_content_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Microsoft::DXIL>" "$<TARGET_FILE_DIR:atlas_renderer_content_tests>")
endif()
atlas_configure_test(atlas_renderer_content_tests)
gtest_discover_tests(atlas_renderer_content_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS renderer-content TIMEOUT 60)
