include_guard(GLOBAL)

if(ATLAS_ENABLE_ASAN)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
    # /RTC and Edit and Continue are incompatible with MSVC AddressSanitizer.
    foreach(flag_variable IN ITEMS CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG
            CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        string(REGEX REPLACE "[/-]RTC[1su]+" "" sanitized_flags "${${flag_variable}}")
        set(${flag_variable} "${sanitized_flags}" CACHE STRING "C++ flags for ASan build" FORCE)
    endforeach()
endif()
