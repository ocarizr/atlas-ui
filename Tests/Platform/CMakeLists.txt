add_executable(atlas_dependency_tests DependencyTests.cpp WindowsApiTests.cpp)
target_link_libraries(atlas_dependency_tests PRIVATE
    Atlas::EcsDependencies Atlas::ContentDependencies Atlas::PlatformDependencies
    Atlas::VideoDependencies Atlas::AudioDependencies GTest::gtest_main)
atlas_configure_test(atlas_dependency_tests)
gtest_discover_tests(atlas_dependency_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS dependencies TIMEOUT 15)
