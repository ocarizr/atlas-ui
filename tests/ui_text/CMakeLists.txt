add_executable(atlas_ui_text_tests atlas_tests.cpp)
target_link_libraries(atlas_ui_text_tests PRIVATE atlas::ui_text GTest::gtest_main)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_ui_text_tests PRIVATE ../platform/windows/ui_text/layout_tests.cpp
        ../platform/windows/ui_text/render_tests.cpp)
    add_custom_command(TARGET atlas_ui_text_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Microsoft::DXIL>" "$<TARGET_FILE_DIR:atlas_ui_text_tests>")
endif()
atlas_configure_test(atlas_ui_text_tests)
gtest_discover_tests(atlas_ui_text_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS ui-text TIMEOUT 30)
