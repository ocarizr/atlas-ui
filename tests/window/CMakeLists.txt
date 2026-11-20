add_executable(atlas_window_tests window_contract_tests.cpp)
target_link_libraries(atlas_window_tests PRIVATE atlas::window GTest::gtest_main)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_window_tests PRIVATE ../platform/windows/window/input_tests.cpp)
    target_compile_definitions(atlas_window_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(atlas_window_tests PRIVATE user32)
    target_sources(atlas_window_tests PRIVATE ../platform/windows/window/window_tests.cpp)
    target_sources(atlas_window_tests PRIVATE ../platform/windows/window/presentation_target_tests.cpp)
endif()
atlas_configure_test(atlas_window_tests)
# Hosted cold startup can exceed GoogleTest's five-second default before any case runs.
gtest_discover_tests(atlas_window_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS window TIMEOUT 30)
