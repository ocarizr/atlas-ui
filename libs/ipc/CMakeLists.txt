set(atlas_protocol_dir "${PROJECT_BINARY_DIR}/generated/protocols")
file(MAKE_DIRECTORY "${atlas_protocol_dir}")
set(atlas_protocol_source "${PROJECT_SOURCE_DIR}/protocols/session.proto")
add_custom_command(
    OUTPUT "${atlas_protocol_dir}/session.pb.cc" "${atlas_protocol_dir}/session.pb.h"
        "${atlas_protocol_dir}/session.grpc.pb.cc" "${atlas_protocol_dir}/session.grpc.pb.h"
    COMMAND protobuf::protoc
        "--proto_path=${PROJECT_SOURCE_DIR}/protocols"
        "--cpp_out=${atlas_protocol_dir}" "--grpc_out=${atlas_protocol_dir}"
        "--plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>" "${atlas_protocol_source}"
    DEPENDS "${atlas_protocol_source}" protobuf::protoc gRPC::grpc_cpp_plugin
    VERBATIM)
add_library(atlas_protocol STATIC
    "${atlas_protocol_dir}/session.pb.cc" "${atlas_protocol_dir}/session.grpc.pb.cc")
add_library(atlas::protocol ALIAS atlas_protocol)
target_include_directories(atlas_protocol SYSTEM PUBLIC
    $<BUILD_INTERFACE:${atlas_protocol_dir}> $<INSTALL_INTERFACE:include>)
# Generated headers travel with the compiled protocol library.
install(DIRECTORY "${atlas_protocol_dir}/" DESTINATION include FILES_MATCHING PATTERN "*.h")
target_include_directories(atlas_protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(atlas_protocol PUBLIC cxx_std_20)
target_link_libraries(atlas_protocol PUBLIC protobuf::libprotobuf gRPC::grpc++)
target_compile_options(atlas_protocol PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/wd4996>")

atlas_export_library(atlas_protocol ipc protocol)
