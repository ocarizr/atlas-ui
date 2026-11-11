add_library(atlas_ui_text STATIC src/text_layout.cpp src/glyph_atlas.cpp src/glyph_paint.cpp
    src/atlas_upload.cpp)
add_library(atlas::ui_text ALIAS atlas_ui_text)
target_include_directories(atlas_ui_text PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(atlas_ui_text PUBLIC cxx_std_20)
target_link_libraries(atlas_ui_text PUBLIC atlas::ui_renderer)
atlas_configure_target(atlas_ui_text)
if(ATLAS_TARGET_PLATFORM STREQUAL "windows" AND ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_ui_text PRIVATE ../platform/windows/ui_text/text_layout.cpp
        ../platform/windows/ui_text/glyph_capture.cpp ../platform/windows/ui_text/glyph_raster.cpp)
    target_compile_definitions(atlas_ui_text PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(atlas_ui_text PRIVATE dwrite)
else()
    target_sources(atlas_ui_text PRIVATE src/unavailable_layout.cpp)
endif()
atlas_export_library(atlas_ui_text ui_text ui_text)
