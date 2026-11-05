add_executable(atlas_ui_images_tests cache_tests.cpp)
target_link_libraries(atlas_ui_images_tests PRIVATE atlas::ui_images GTest::gtest_main)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_ui_images_tests PRIVATE ../platform/windows/ui_images/residency_tests.cpp)
    add_custom_command(TARGET atlas_ui_images_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Microsoft::DXIL>" "$<TARGET_FILE_DIR:atlas_ui_images_tests>")
endif()
atlas_configure_test(atlas_ui_images_tests)
gtest_discover_tests(atlas_ui_images_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS ui-images TIMEOUT 30)
