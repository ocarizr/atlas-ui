include_guard(GLOBAL)

find_package(EnTT CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(directx-dxc CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
endif()

add_library(atlas_ecs_dependencies INTERFACE)
add_library(Atlas::EcsDependencies ALIAS atlas_ecs_dependencies)
target_link_libraries(atlas_ecs_dependencies INTERFACE EnTT::EnTT)

add_library(atlas_content_dependencies INTERFACE)
add_library(Atlas::ContentDependencies ALIAS atlas_content_dependencies)
target_link_libraries(atlas_content_dependencies INTERFACE pugixml::pugixml yaml-cpp::yaml-cpp)

add_library(atlas_audio_dependencies INTERFACE)
add_library(Atlas::AudioDependencies ALIAS atlas_audio_dependencies)
target_link_libraries(atlas_audio_dependencies INTERFACE Opus::opus xaudio2 ole32)

add_library(atlas_platform_dependencies INTERFACE)
add_library(Atlas::PlatformDependencies ALIAS atlas_platform_dependencies)
target_compile_definitions(atlas_platform_dependencies INTERFACE _WIN32_WINNT=0x0A00)
target_link_libraries(atlas_platform_dependencies INTERFACE
    d3d12 dxgi dxguid dwrite windowscodecs xaudio2 ole32
    Microsoft::DirectXShaderCompiler)

include(cmake/AtlasVideoDependencies.cmake)
