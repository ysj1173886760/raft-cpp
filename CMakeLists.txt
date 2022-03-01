cmake_minimum_required(VERSION 3.15)
project(Raft)

include(common.cmake)

set(src_dir ${CMAKE_SOURCE_DIR}/src)

message("${src_dir}")

get_filename_component(rf_proto "src/raft.proto" ABSOLUTE)
get_filename_component(rf_proto_path "${rf_proto}" PATH)

set(rf_proto_srcs "${src_dir}/raft.pb.cc")
set(rf_proto_hdrs "${src_dir}/raft.pb.h")
set(rf_grpc_srcs "${src_dir}/raft.grpc.pb.cc")
set(rf_grpc_hdrs "${src_dir}/raft.grpc.pb.h")

add_custom_command(
      OUTPUT "${rf_proto_srcs}" "${rf_proto_hdrs}" "${rf_grpc_srcs}" "${rf_grpc_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${src_dir}"
        --cpp_out "${src_dir}"
        -I "${rf_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${rf_proto}"
      DEPENDS "${rf_proto}")

add_library(rf_grpc_proto
  ${rf_grpc_srcs}
  ${rf_grpc_hdrs}
  ${rf_proto_srcs}
  ${rf_proto_hdrs})
target_link_libraries(rf_grpc_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})


add_executable(Raft src/raft.cpp)
target_link_libraries(Raft ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF} rf_grpc_proto)