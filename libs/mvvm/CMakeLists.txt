add_library(atlas_mvvm STATIC src/schema.cpp src/schema_signals.cpp src/validation.cpp src/view_model.cpp
    src/relative_updates.cpp src/commands.cpp src/batches.cpp src/subscriptions.cpp src/dispatch.cpp)
add_library(atlas::mvvm ALIAS atlas_mvvm)
target_compile_features(atlas_mvvm PUBLIC cxx_std_20)
target_include_directories(atlas_mvvm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(atlas_mvvm PUBLIC atlas::diagnostics)
atlas_configure_target(atlas_mvvm)
atlas_export_library(atlas_mvvm mvvm mvvm)
