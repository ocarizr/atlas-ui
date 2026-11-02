add_executable(atlas_renderer_tests renderer_contract_tests.cpp)
target_link_libraries(atlas_renderer_tests PRIVATE atlas::renderer GTest::gtest_main)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_renderer_tests PRIVATE ../platform/windows/renderer/presentation_tests.cpp)
    add_custom_command(TARGET atlas_renderer_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Microsoft::DXIL>" "$<TARGET_FILE_DIR:atlas_renderer_tests>")
endif()
atlas_configure_test(atlas_renderer_tests)
gtest_discover_tests(atlas_renderer_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS renderer TIMEOUT 30)
