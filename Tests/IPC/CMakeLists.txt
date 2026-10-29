add_executable(atlas_protocol_tests ProtocolTests.cpp)
target_link_libraries(atlas_protocol_tests PRIVATE Atlas::Protocol GTest::gtest_main)
atlas_configure_test(atlas_protocol_tests)
gtest_discover_tests(atlas_protocol_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS ipc TIMEOUT 15)
