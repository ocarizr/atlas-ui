add_library(atlas_ui_mvvm STATIC src/action_handler.cpp src/binding_session.cpp src/conversions.cpp src/publication.cpp)
add_library(atlas::ui_mvvm ALIAS atlas_ui_mvvm)
target_compile_features(atlas_ui_mvvm PUBLIC cxx_std_20)
target_include_directories(atlas_ui_mvvm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(atlas_ui_mvvm PUBLIC atlas::mvvm atlas::ui)
atlas_configure_target(atlas_ui_mvvm)
atlas_export_library(atlas_ui_mvvm ui_mvvm ui_mvvm)
