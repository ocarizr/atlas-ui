add_library(atlas_application STATIC src/entry_point.cpp)
add_library(atlas::application ALIAS atlas_application)
target_include_directories(atlas_application PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(atlas_application PUBLIC cxx_std_20)
target_link_libraries(atlas_application PRIVATE atlas::jobs atlas::protocol spdlog::spdlog)
atlas_configure_target(atlas_application)

atlas_export_library(atlas_application application application)
