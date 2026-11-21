add_executable(atlas_editor_flow_tests document_tests.cpp document_failure_tests.cpp
    session_tests.cpp persistence_tests.cpp workspace_tests.cpp inspector_tests.cpp inspector_failure_tests.cpp canvas_tests.cpp)
target_link_libraries(atlas_editor_flow_tests PRIVATE atlas::editor_flow GTest::gtest_main)
target_compile_definitions(atlas_editor_flow_tests PRIVATE
    ATLAS_FLOW_DRAFT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/data/settings_flow.xml")
atlas_configure_test(atlas_editor_flow_tests)
gtest_discover_tests(atlas_editor_flow_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS editor-flow TIMEOUT 30)
