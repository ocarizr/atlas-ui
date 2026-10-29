include_guard(GLOBAL)

find_package(unofficial-libwebm CONFIG REQUIRED)
find_path(ATLAS_DAV1D_INCLUDE_DIR dav1d/dav1d.h REQUIRED)
find_path(ATLAS_LIBWEBM_INCLUDE_DIR mkvparser/mkvreader.h PATH_SUFFIXES webm REQUIRED)
set(atlas_dependency_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
find_library(ATLAS_DAV1D_RELEASE NAMES dav1d
    PATHS "${atlas_dependency_prefix}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(ATLAS_DAV1D_DEBUG NAMES dav1d
    PATHS "${atlas_dependency_prefix}/debug/lib" NO_DEFAULT_PATH REQUIRED)
add_library(atlas_dav1d UNKNOWN IMPORTED)
set_target_properties(atlas_dav1d PROPERTIES
    IMPORTED_LOCATION "${ATLAS_DAV1D_RELEASE}"
    IMPORTED_LOCATION_DEBUG "${ATLAS_DAV1D_DEBUG}"
    INTERFACE_INCLUDE_DIRECTORIES "${ATLAS_DAV1D_INCLUDE_DIR}")
add_library(atlas_video_dependencies INTERFACE)
add_library(atlas::video_dependencies ALIAS atlas_video_dependencies)
target_include_directories(atlas_video_dependencies SYSTEM INTERFACE "${ATLAS_LIBWEBM_INCLUDE_DIR}")
target_link_libraries(atlas_video_dependencies INTERFACE atlas_dav1d unofficial::libwebm::libwebm)
