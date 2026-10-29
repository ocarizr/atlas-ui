add_executable(atlas_windows_tests windows_api_tests.cpp)
target_link_libraries(atlas_windows_tests PRIVATE atlas::windows_dependencies GTest::gtest_main)
atlas_configure_test(atlas_windows_tests)
gtest_discover_tests(atlas_windows_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS windows TIMEOUT 15)
