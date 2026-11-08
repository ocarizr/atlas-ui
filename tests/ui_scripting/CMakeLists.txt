add_executable(atlas_ui_scripting_tests event_tests.cpp async_tests.cpp async_failure_tests.cpp)
target_sources(atlas_ui_scripting_tests PRIVATE domain_tests.cpp)
target_link_libraries(atlas_ui_scripting_tests PRIVATE atlas::ui_scripting GTest::gtest_main)
atlas_configure_test(atlas_ui_scripting_tests)
gtest_discover_tests(atlas_ui_scripting_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS ui-scripting TIMEOUT 30)
