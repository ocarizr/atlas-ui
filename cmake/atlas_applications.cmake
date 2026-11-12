function(atlas_add_application target source output label argument)
    if(NOT label IN_LIST atlas_requested_components)
        return()
    endif()
    add_executable(${target} "${PROJECT_SOURCE_DIR}/${source}")
    target_link_libraries(${target} PRIVATE atlas::application)
    if(label STREQUAL "game")
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/apps/common/renderer_host.cpp")
        target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/apps/common")
        target_link_libraries(${target} PRIVATE atlas::ui_images atlas::ui_renderer atlas::renderer_jobs atlas::renderer_content atlas::render_host atlas::memory atlas::diagnostics)
        file(SIZE "${PROJECT_SOURCE_DIR}/assets/renderer/checker.bmp" atlas_demo_image_size)
        file(SIZE "${PROJECT_SOURCE_DIR}/assets/renderer/quad.hlsl" atlas_demo_shader_size)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${PROJECT_SOURCE_DIR}/assets/renderer/checker.bmp" "${PROJECT_SOURCE_DIR}/assets/renderer/quad.hlsl")
        target_compile_definitions(${target} PRIVATE ATLAS_DEMO_IMAGE_SIZE=${atlas_demo_image_size}
            ATLAS_DEMO_SHADER_SIZE=${atlas_demo_shader_size})
        add_custom_target(${target}_content
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/content"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/assets/renderer/checker.bmp" "${PROJECT_SOURCE_DIR}/assets/renderer/quad.hlsl"
                "$<TARGET_FILE_DIR:${target}>/content")
        add_dependencies(${target} ${target}_content)
        install(FILES "${PROJECT_SOURCE_DIR}/assets/renderer/checker.bmp" "${PROJECT_SOURCE_DIR}/assets/renderer/quad.hlsl"
            DESTINATION bin/content)
    elseif(label STREQUAL "editor")
        target_sources(${target} PRIVATE apps/editor/document_session.cpp apps/editor/editor_host.cpp
            apps/editor/editor_view.cpp apps/editor/editor_graphics.cpp apps/editor/panel_scene.cpp
            apps/editor/panels.cpp)
        target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/apps/common"
            "${PROJECT_SOURCE_DIR}/apps/editor")
        target_link_libraries(${target} PRIVATE atlas::ui_content atlas::ui_text atlas::renderer_jobs atlas::memory)
        if(ATLAS_BUILD_WINDOWS_INTEGRATION)
            target_sources(${target} PRIVATE libs/platform/windows/editor/document_file.cpp)
            target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
        else()
            target_sources(${target} PRIVATE apps/editor/unavailable_file.cpp)
        endif()
    endif()
    if(label STREQUAL "game" OR label STREQUAL "editor")
        if(ATLAS_BUILD_WINDOWS_INTEGRATION)
            if(MSVC)
                target_sources(${target} PRIVATE
                    "${PROJECT_SOURCE_DIR}/libs/platform/windows/window/application.manifest")
            endif()
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Microsoft::DXIL>" "$<TARGET_FILE_DIR:${target}>")
            install(FILES "$<TARGET_FILE:Microsoft::DXIL>" DESTINATION bin)
        endif()
    endif()
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output}")
    atlas_configure_target(${target})
    install(TARGETS ${target} RUNTIME_DEPENDENCY_SET atlas_runtime RUNTIME DESTINATION bin)
    if(BUILD_TESTING)
        add_test(NAME "${output}.entry_point" COMMAND ${target} "${argument}")
        set_tests_properties("${output}.entry_point" PROPERTIES LABELS "${label}" TIMEOUT 15)
        if(ATLAS_BUILD_WINDOWS_INTEGRATION AND (label STREQUAL "game" OR label STREQUAL "editor"))
            add_test(NAME "${output}.graphics_smoke" COMMAND ${target} --graphics-smoke-test)
            set_tests_properties("${output}.graphics_smoke" PROPERTIES LABELS "${label}" TIMEOUT 30)
        endif()
    endif()
endfunction()

atlas_add_application(atlas_game apps/game/main.cpp atlas_game game --smoke-test)
atlas_add_application(atlas_editor apps/editor/main.cpp atlas_editor editor --smoke-test)
atlas_add_application(atlas_cook tools/cook/main.cpp atlas_cook tools --help)
atlas_add_application(atlas_package tools/package/main.cpp atlas_package tools --help)
if(BUILD_TESTING AND editor IN_LIST atlas_requested_components)
    add_executable(atlas_editor_tests "${PROJECT_SOURCE_DIR}/tests/editor/session_tests.cpp"
        "${PROJECT_SOURCE_DIR}/apps/editor/document_session.cpp")
    target_include_directories(atlas_editor_tests PRIVATE "${PROJECT_SOURCE_DIR}/apps/editor"
        "${PROJECT_SOURCE_DIR}/apps/common")
    target_link_libraries(atlas_editor_tests PRIVATE atlas::ui_content GTest::gtest_main)
    if(ATLAS_BUILD_WINDOWS_INTEGRATION)
        target_sources(atlas_editor_tests PRIVATE tests/platform/windows/editor/authoring_tests.cpp
            libs/platform/windows/editor/document_file.cpp apps/editor/editor_view.cpp
            apps/editor/panels.cpp apps/editor/panel_scene.cpp)
        target_link_libraries(atlas_editor_tests PRIVATE atlas::ui_text)
        target_compile_definitions(atlas_editor_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    endif()
    atlas_configure_test(atlas_editor_tests)
    gtest_discover_tests(atlas_editor_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS editor TIMEOUT 30)
endif()
