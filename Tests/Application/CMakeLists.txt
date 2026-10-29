add_executable(atlas_application_tests EntryPointTests.cpp)
target_link_libraries(atlas_application_tests PRIVATE Atlas::Application GTest::gtest_main)
atlas_configure_test(atlas_application_tests)
gtest_discover_tests(atlas_application_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS application TIMEOUT 15)
