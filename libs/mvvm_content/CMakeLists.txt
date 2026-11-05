add_library(atlas_mvvm_content STATIC src/scalars.cpp src/xml_validation.cpp src/xml_read.cpp
    src/xml_write.cpp src/codegen.cpp)
add_library(atlas::mvvm_content ALIAS atlas_mvvm_content)
target_compile_features(atlas_mvvm_content PUBLIC cxx_std_20)
target_include_directories(atlas_mvvm_content PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(atlas_mvvm_content PUBLIC atlas::mvvm PRIVATE pugixml::pugixml)
atlas_configure_target(atlas_mvvm_content)
atlas_export_library(atlas_mvvm_content mvvm_content mvvm_content)
