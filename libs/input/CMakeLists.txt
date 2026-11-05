add_library(atlas_input STATIC src/input_system.cpp src/event_queue.cpp src/processors.cpp
    src/action_mapping.cpp src/device_state.cpp src/action_aggregation.cpp src/action_transitions.cpp src/input_dispatch.cpp)
add_library(atlas::input ALIAS atlas_input)
target_include_directories(atlas_input PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(atlas_input PUBLIC cxx_std_20)
target_link_libraries(atlas_input PUBLIC atlas::diagnostics)
if(ATLAS_TARGET_PLATFORM STREQUAL "windows" AND ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_input PRIVATE ../platform/windows/input/win32_input.cpp)
    target_compile_definitions(atlas_input PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(atlas_input PRIVATE user32)
endif()
atlas_configure_target(atlas_input)
atlas_export_library(atlas_input input input)
