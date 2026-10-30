add_library(atlas_jobs STATIC src/job_system.cpp src/job_graph.cpp src/graph_state.cpp
    src/scheduler_workers.cpp src/execution_context.cpp src/job_dispatcher.cpp
    src/task_handle.cpp src/task_submission.cpp src/task_completion.cpp)
add_library(atlas::jobs ALIAS atlas_jobs)
target_include_directories(atlas_jobs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(atlas_jobs PUBLIC cxx_std_20)
atlas_configure_target(atlas_jobs)

atlas_export_library(atlas_jobs jobs jobs)
