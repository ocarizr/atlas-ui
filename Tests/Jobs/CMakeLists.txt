add_executable(atlas_jobs_tests ExecutionTests.cpp FailureTests.cpp LifecycleTests.cpp)
target_link_libraries(atlas_jobs_tests PRIVATE Atlas::Jobs GTest::gtest_main)
atlas_configure_test(atlas_jobs_tests)
include(GoogleTest)
gtest_discover_tests(atlas_jobs_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS jobs TIMEOUT 20)
