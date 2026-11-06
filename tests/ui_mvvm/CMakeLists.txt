add_executable(atlas_ui_mvvm_tests binding_tests.cpp action_tests.cpp)
target_link_libraries(atlas_ui_mvvm_tests PRIVATE atlas::ui_mvvm GTest::gtest_main)
atlas_configure_test(atlas_ui_mvvm_tests)
gtest_discover_tests(atlas_ui_mvvm_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS ui-mvvm TIMEOUT 30)
target_sources(atlas_ui_mvvm_tests PRIVATE failure_tests.cpp)
target_sources(atlas_ui_mvvm_tests PRIVATE diagnostics_tests.cpp)
