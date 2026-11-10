add_library(atlas_asset_catalog STATIC src/identity.cpp src/catalog.cpp src/xml_read.cpp src/xml_write.cpp)
add_library(atlas::asset_catalog ALIAS atlas_asset_catalog)
target_compile_features(atlas_asset_catalog PUBLIC cxx_std_20)
target_include_directories(atlas_asset_catalog PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(atlas_asset_catalog PUBLIC atlas::diagnostics PRIVATE pugixml::pugixml)
atlas_configure_target(atlas_asset_catalog)
atlas_export_library(atlas_asset_catalog asset_catalog asset_catalog)
