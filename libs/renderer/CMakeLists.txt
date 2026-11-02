add_library(atlas_renderer STATIC src/renderer.cpp src/resource_store.cpp)
target_sources(atlas_renderer PRIVATE src/device_status.cpp)
target_sources(atlas_renderer PRIVATE src/recording.cpp src/recording_plan.cpp)
target_sources(atlas_renderer PRIVATE src/pass_contract.cpp src/pass_renderer.cpp src/pass_validation.cpp)
target_sources(atlas_renderer PRIVATE src/resource_presentation.cpp)
add_library(atlas::renderer ALIAS atlas_renderer)
target_include_directories(atlas_renderer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_include_directories(atlas_renderer PRIVATE src ${PROJECT_SOURCE_DIR}/libs/window/src)
target_compile_features(atlas_renderer PUBLIC cxx_std_20)
target_link_libraries(atlas_renderer PUBLIC atlas::window atlas::diagnostics)
atlas_configure_target(atlas_renderer)
if(ATLAS_TARGET_PLATFORM STREQUAL "windows" AND ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_renderer PRIVATE ../platform/windows/renderer/device_health.cpp)
    target_sources(atlas_renderer PRIVATE ../platform/windows/renderer/gpu_timing.cpp)
    target_sources(atlas_renderer PRIVATE ../platform/windows/renderer/presentation_status.cpp)
    target_sources(atlas_renderer PRIVATE ../platform/windows/renderer/native_device.cpp
        ../platform/windows/renderer/native_surface.cpp ../platform/windows/renderer/triangle_pipeline.cpp
        ../platform/windows/renderer/readback.cpp)
    target_sources(atlas_renderer PRIVATE ../platform/windows/renderer/resource_allocation.cpp
        ../platform/windows/renderer/resource_transfer.cpp ../platform/windows/renderer/resource_capture.cpp)
    target_sources(atlas_renderer PRIVATE ../platform/windows/renderer/pass_pipeline.cpp
        ../platform/windows/renderer/pass_execution.cpp ../platform/windows/renderer/pass_shaders.cpp)
    target_sources(atlas_renderer PRIVATE ../platform/windows/renderer/surface_copy.cpp)
    target_sources(atlas_renderer PRIVATE ../platform/windows/renderer/recording_pool.cpp
        ../platform/windows/renderer/parallel_recording.cpp)
    target_compile_definitions(atlas_renderer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_compile_definitions(atlas_renderer PRIVATE D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED)
    target_link_libraries(atlas_renderer PRIVATE d3d12 dxgi dxguid Microsoft::DirectXShaderCompiler)
    target_link_libraries(atlas_renderer PRIVATE GPUOpen::D3D12MemoryAllocator)
else()
    target_sources(atlas_renderer PRIVATE src/unavailable_backend.cpp)
endif()
atlas_export_library(atlas_renderer renderer renderer)
