add_executable(atlas_dependency_tests dependency_tests.cpp)
target_link_libraries(atlas_dependency_tests PRIVATE
    atlas::ecs_dependencies atlas::content_dependencies
    atlas::video_dependencies atlas::audio_dependencies GTest::gtest_main)
atlas_configure_test(atlas_dependency_tests)
gtest_discover_tests(atlas_dependency_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS dependencies TIMEOUT 15)
