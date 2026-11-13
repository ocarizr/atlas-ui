add_executable(atlas_ui_content_fixture_generator ../mvvm_content/generate_fixture.cpp)
target_link_libraries(atlas_ui_content_fixture_generator PRIVATE atlas::mvvm_content)
atlas_configure_target(atlas_ui_content_fixture_generator)
set(model_fixture "${CMAKE_CURRENT_SOURCE_DIR}/../mvvm_content/data/hud.xml")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/model.cpp")
add_custom_command(OUTPUT "${generated}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>"
    COMMAND $<TARGET_FILE:atlas_ui_content_fixture_generator> "${model_fixture}" "${generated}" make_ui_content_fixture
    DEPENDS atlas_ui_content_fixture_generator "${model_fixture}"
    VERBATIM)
add_executable(atlas_ui_content_tests xml_tests.cpp composition_tests.cpp composition_failure_tests.cpp
    animation_tests.cpp "${generated}")
target_compile_definitions(atlas_ui_content_tests PRIVATE
    ATLAS_UI_DOCUMENT="${CMAKE_CURRENT_SOURCE_DIR}/data/dashboard.xml"
    ATLAS_UI_MODEL="${model_fixture}")
target_link_libraries(atlas_ui_content_tests PRIVATE atlas::ui_content GTest::gtest_main)
atlas_configure_test(atlas_ui_content_tests)
gtest_discover_tests(atlas_ui_content_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS ui-content TIMEOUT 30)
