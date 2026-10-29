include_guard(GLOBAL)

# Apply only to Atlas-owned compiled targets. Each library must separately
# declare usage requirements for its public headers (including C++20).
function(atlas_configure_target target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    target_link_libraries(${target} PRIVATE atlas::instrumentation)

    if(ATLAS_ENABLE_MSVC_ANALYSIS)
        target_compile_options(${target} PRIVATE /analyze /analyze:external-)
    endif()
    if(ATLAS_ENABLE_CLANG_TIDY)
        set(tidy_command "${ATLAS_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy")
        if(MSVC)
            list(APPEND tidy_command --extra-arg=/EHsc)
        endif()
        set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY "${tidy_command}")
    endif()
endfunction()

function(atlas_configure_test target)
    atlas_configure_target(${target})
    if(ATLAS_ENABLE_MSVC_ANALYSIS)
        # GoogleTest assertion macros expand constant comparisons at call sites.
        target_compile_options(${target} PRIVATE /wd6326)
    endif()
endfunction()
