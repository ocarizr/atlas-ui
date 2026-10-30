include_guard(GLOBAL)

# The applications register install targets before this module is included.
install(FILES
    "${PROJECT_SOURCE_DIR}/README.md"
    "${PROJECT_SOURCE_DIR}/CONTRIBUTING.md"
    "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION .)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/" DESTINATION docs
    FILES_MATCHING PATTERN "*.md")
configure_file("${PROJECT_SOURCE_DIR}/cmake/package_manifest.xml.in"
    "${PROJECT_BINARY_DIR}/package_manifest.xml" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/package_manifest.xml" DESTINATION .)

set(atlas_vendor_prefixes ${CMAKE_PREFIX_PATH})
if(VCPKG_INSTALLED_DIR)
    list(APPEND atlas_vendor_prefixes "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()
set(atlas_runtime_directories)
foreach(prefix IN LISTS atlas_vendor_prefixes)
    list(APPEND atlas_runtime_directories "${prefix}/$<$<CONFIG:Debug>:debug/>bin")
    file(GLOB copyrights "${prefix}/share/*/copyright")
    foreach(copyright IN LISTS copyrights)
        get_filename_component(dependency_directory "${copyright}" DIRECTORY)
        get_filename_component(dependency_name "${dependency_directory}" NAME)
        install(FILES "${copyright}" DESTINATION "licenses/${dependency_name}")
    endforeach()
endforeach()
if(ATLAS_ENABLE_ASAN AND MSVC)
    get_filename_component(atlas_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    list(APPEND atlas_runtime_directories "${atlas_compiler_directory}")
endif()
list(REMOVE_DUPLICATES atlas_runtime_directories)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME atlas_ui)
set(CPACK_PACKAGE_VENDOR "Atlas UI")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
install(RUNTIME_DEPENDENCY_SET atlas_runtime
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
    DIRECTORIES ${atlas_runtime_directories}
    RUNTIME DESTINATION bin)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Atlas UI application and job-system bootstrap")
set(CPACK_PACKAGE_FILE_NAME "atlas_ui-${PROJECT_VERSION}-windows-x64")
include(CPack)
