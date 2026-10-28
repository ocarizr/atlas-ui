include_guard(GLOBAL)

# Initial deliverable: development documentation. Real libraries/apps must add
# their own install(TARGETS ...) rules when implemented.
install(FILES
    "${PROJECT_SOURCE_DIR}/README.md"
    "${PROJECT_SOURCE_DIR}/CONTRIBUTING.md"
    "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION .)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/" DESTINATION docs
    FILES_MATCHING PATTERN "*.md")
configure_file("${PROJECT_SOURCE_DIR}/cmake/package-manifest.xml.in"
    "${PROJECT_BINARY_DIR}/package-manifest.xml" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/package-manifest.xml" DESTINATION .)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME AtlasUI)
set(CPACK_PACKAGE_VENDOR "Atlas UI")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Atlas UI development foundation")
set(CPACK_PACKAGE_FILE_NAME "AtlasUI-${PROJECT_VERSION}-windows-x64")
include(CPack)
