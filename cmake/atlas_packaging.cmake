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

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME atlas_ui)
set(CPACK_PACKAGE_VENDOR "Atlas UI")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
install(RUNTIME_DEPENDENCY_SET atlas_runtime
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
    DIRECTORIES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin"
    RUNTIME DESTINATION bin)
file(GLOB atlas_dependency_copyrights
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*/copyright")
foreach(copyright IN LISTS atlas_dependency_copyrights)
    get_filename_component(dependency_directory "${copyright}" DIRECTORY)
    get_filename_component(dependency_name "${dependency_directory}" NAME)
    install(FILES "${copyright}" DESTINATION "licenses/${dependency_name}")
endforeach()
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Atlas UI application and job-system bootstrap")
set(CPACK_PACKAGE_FILE_NAME "atlas_ui-${PROJECT_VERSION}-windows-x64")
include(CPack)
