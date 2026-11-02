if(NOT TARGET Microsoft::DirectXShaderCompiler)
    find_package(directx-dxc CONFIG REQUIRED)
endif()

add_library(atlas_windows_dependencies INTERFACE)
add_library(atlas::windows_dependencies ALIAS atlas_windows_dependencies)
target_compile_definitions(atlas_windows_dependencies INTERFACE
    _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(atlas_windows_dependencies INTERFACE
    d3d12 dxgi dxguid dwrite windowscodecs xaudio2 ole32
    Microsoft::DirectXShaderCompiler)
