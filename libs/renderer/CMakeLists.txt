add_library(atlas_renderer STATIC src/renderer.cpp)
add_library(atlas::renderer ALIAS atlas_renderer)
target_include_directories(atlas_renderer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_include_directories(atlas_renderer PRIVATE src ${PROJECT_SOURCE_DIR}/libs/window/src)
target_compile_features(atlas_renderer PUBLIC cxx_std_20)
target_link_libraries(atlas_renderer PUBLIC atlas::window)
atlas_configure_target(atlas_renderer)
if(ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_renderer PRIVATE ../platform/windows/renderer/native_device.cpp
        ../platform/windows/renderer/native_surface.cpp ../platform/windows/renderer/triangle_pipeline.cpp
        ../platform/windows/renderer/readback.cpp)
    target_compile_definitions(atlas_renderer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(atlas_renderer PRIVATE d3d12 dxgi dxguid Microsoft::DirectXShaderCompiler)
else()
    target_sources(atlas_renderer PRIVATE src/unavailable_backend.cpp)
endif()
atlas_export_library(atlas_renderer renderer renderer)
