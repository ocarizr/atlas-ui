add_executable(atlas_mvvm_fixture_generator generate_fixture.cpp)
target_link_libraries(atlas_mvvm_fixture_generator PRIVATE atlas::mvvm_content)
atlas_configure_target(atlas_mvvm_fixture_generator)
set(fixture "${CMAKE_CURRENT_SOURCE_DIR}/data/hud.xml")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/fixture.cpp")
add_custom_command(OUTPUT "${generated}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>"
    COMMAND $<TARGET_FILE:atlas_mvvm_fixture_generator> "${fixture}" "${generated}"
    DEPENDS atlas_mvvm_fixture_generator "${fixture}"
    VERBATIM)
set(large_fixture "${CMAKE_CURRENT_SOURCE_DIR}/data/large_text.xml")
set(large_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/large_fixture.cpp")
add_custom_command(OUTPUT "${large_generated}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>"
    COMMAND $<TARGET_FILE:atlas_mvvm_fixture_generator> "${large_fixture}" "${large_generated}" make_large_fixture
    DEPENDS atlas_mvvm_fixture_generator "${large_fixture}"
    VERBATIM)
add_executable(atlas_mvvm_content_tests xml_tests.cpp generated_tests.cpp "${generated}" "${large_generated}")
target_link_libraries(atlas_mvvm_content_tests PRIVATE atlas::mvvm_content GTest::gtest_main)
target_compile_definitions(atlas_mvvm_content_tests PRIVATE ATLAS_MVVM_XML_FIXTURE="${fixture}")
atlas_configure_test(atlas_mvvm_content_tests)
gtest_discover_tests(atlas_mvvm_content_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS mvvm-content TIMEOUT 30)
target_sources(atlas_mvvm_content_tests PRIVATE boundary_tests.cpp)
target_sources(atlas_mvvm_content_tests PRIVATE diagnostics_tests.cpp)
