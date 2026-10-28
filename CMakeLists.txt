cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake --preset windows-msvc")
endif()

option(ATLAS_ENABLE_ASAN "Instrument Atlas targets with AddressSanitizer" OFF)
option(ATLAS_ENABLE_TSAN "Reserved: unsupported on native Windows" OFF)
option(ATLAS_ENABLE_COVERAGE "Instrument Atlas targets with LLVM coverage" OFF)
option(ATLAS_ENABLE_CLANG_TIDY "Analyze Atlas targets with clang-tidy" OFF)
option(ATLAS_ENABLE_MSVC_ANALYSIS "Analyze Atlas targets with MSVC /analyze" OFF)

# Use /Zi rather than Edit and Continue in this instrumentation profile.
if(ATLAS_ENABLE_ASAN)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
endif()

set(ATLAS_VERSION "0.1.0" CACHE STRING "Atlas semantic version")
if(NOT ATLAS_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "ATLAS_VERSION must be MAJOR.MINOR.PATCH without leading zeros.")
endif()
project(AtlasUI VERSION ${ATLAS_VERSION} LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Atlas UI supports native Windows only.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "Use MSVC or clang-cl with the MSVC ABI.")
endif()

# CMake initializes Debug with /RTC1 on MSVC. Remove incompatible runtime
# checks from this build profile, including existing cache entries.
if(ATLAS_ENABLE_ASAN)
    foreach(flag_variable IN ITEMS CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG
            CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        string(REGEX REPLACE "[/-]RTC[1su]+" "" sanitized_flags "${${flag_variable}}")
        set(${flag_variable} "${sanitized_flags}" CACHE STRING "C++ flags for ASan build" FORCE)
    endforeach()
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/AtlasInstrumentation.cmake)
include(cmake/AtlasTargetOptions.cmake)
include(CTest)
include(cmake/AtlasPackaging.cmake)

# Add libraries, apps and GoogleTest targets alongside real features.
# An empty build is not a runtime or a successful test suite.
message(STATUS "Atlas development foundation configured; no runtime targets exist yet.")
