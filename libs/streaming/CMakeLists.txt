add_library(atlas_streaming STATIC src/asset_streamer.cpp src/range_validation.cpp
    src/buffer_memory.cpp src/streamer_state.cpp src/memory_reads.cpp src/file_pump.cpp)
add_library(atlas::streaming ALIAS atlas_streaming)
target_include_directories(atlas_streaming PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(atlas_streaming PUBLIC cxx_std_20)
target_include_directories(atlas_streaming PRIVATE src)
if(ATLAS_TARGET_PLATFORM STREQUAL "windows" AND ATLAS_BUILD_WINDOWS_INTEGRATION)
    target_sources(atlas_streaming PRIVATE ../platform/windows/streaming/iocp_api.cpp
        ../platform/windows/streaming/iocp_backend.cpp ../platform/windows/streaming/iocp_submission.cpp)
    target_compile_definitions(atlas_streaming PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(atlas_streaming PRIVATE kernel32)
else()
    target_sources(atlas_streaming PRIVATE src/asset_reader.cpp src/platform/standard/file_reader.cpp
        src/platform/standard/file_io.cpp)
endif()
target_link_libraries(atlas_streaming PUBLIC atlas::jobs)
atlas_configure_target(atlas_streaming)

atlas_export_library(atlas_streaming streaming streaming)
