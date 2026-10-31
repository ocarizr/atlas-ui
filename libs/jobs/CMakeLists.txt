cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(atlas_jobs VERSION 0.1.0 LANGUAGES CXX)
endif()

add_library(atlas_jobs STATIC src/job_system.cpp src/job_graph.cpp src/graph_state.cpp
    src/scheduler_workers.cpp src/execution_context.cpp src/job_dispatcher.cpp
    src/task_handle.cpp src/task_submission.cpp src/task_completion.cpp src/task_execution.cpp
    src/access_registry.cpp src/access_guard.cpp src/scratch_resource.cpp src/task_trace.cpp)
add_library(atlas::jobs ALIAS atlas_jobs)
target_include_directories(atlas_jobs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(atlas_jobs PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(atlas_jobs PUBLIC Threads::Threads)
set(ATLAS_JOB_SANITIZER AUTO CACHE STRING "Job access checks: AUTO (Debug), ON or OFF")
set_property(CACHE ATLAS_JOB_SANITIZER PROPERTY STRINGS AUTO ON OFF)
if(ATLAS_JOB_SANITIZER STREQUAL "AUTO")
    set(atlas_jobs_checked "$<CONFIG:Debug>")
elseif(ATLAS_JOB_SANITIZER STREQUAL "ON")
    set(atlas_jobs_checked 1)
elseif(ATLAS_JOB_SANITIZER STREQUAL "OFF")
    set(atlas_jobs_checked 0)
else()
    message(FATAL_ERROR "ATLAS_JOB_SANITIZER must be AUTO, ON or OFF")
endif()
target_compile_definitions(atlas_jobs PUBLIC ATLAS_JOBS_SANITIZER=${atlas_jobs_checked})
if(COMMAND atlas_configure_target)
    atlas_configure_target(atlas_jobs)
else()
    if(MSVC)
        target_compile_options(atlas_jobs PRIVATE /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(atlas_jobs PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

if(COMMAND atlas_export_library)
    atlas_export_library(atlas_jobs jobs jobs)
else()
    set_target_properties(atlas_jobs PROPERTIES EXPORT_NAME jobs)
    install(TARGETS atlas_jobs EXPORT atlas_jobs_exports ARCHIVE DESTINATION lib)
    install(EXPORT atlas_jobs_exports FILE jobs.cmake NAMESPACE atlas:: DESTINATION share/atlas)
    install(DIRECTORY include/ DESTINATION include)
endif()
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/atlas_jobs_config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/atlas_jobsConfig.cmake"
    INSTALL_DESTINATION share/atlas_jobs)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/atlas_jobsConfigVersion.cmake"
    VERSION 0.1.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/atlas_jobsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/atlas_jobsConfigVersion.cmake" DESTINATION share/atlas_jobs)

install(FILES README.md DESTINATION share/atlas_jobs)
