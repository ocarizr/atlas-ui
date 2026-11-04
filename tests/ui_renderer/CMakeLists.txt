add_executable(atlas_ui_renderer_tests packet_tests.cpp)
target_link_libraries(atlas_ui_renderer_tests PRIVATE atlas::ui_renderer GTest::gtest_main)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_ui_renderer_tests PRIVATE ../platform/windows/ui_renderer/pixel_tests.cpp)
    add_custom_command(TARGET atlas_ui_renderer_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Microsoft::DXIL>" "$<TARGET_FILE_DIR:atlas_ui_renderer_tests>")
endif()
atlas_configure_test(atlas_ui_renderer_tests)
gtest_discover_tests(atlas_ui_renderer_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS ui-renderer TIMEOUT 60)
