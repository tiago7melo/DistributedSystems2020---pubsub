cmake_minimum_required(VERSION 3.10)
project(distributed-systems VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_FLAGS "-std=c++14")

find_package(Threads REQUIRED)

## GRPC
# Automatically fetch grpc from github

include(FetchContent)
FetchContent_Declare(
  grpc
  GIT_REPOSITORY https://github.com/grpc/grpc.git
  GIT_TAG        v1.33.2
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(grpc)

## Generate source files from proto file
set(_PROTOBUF_PROTOC $<TARGET_FILE:protoc>)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:grpc_cpp_plugin>)

get_filename_component(proto "./protos/pubsub.proto" ABSOLUTE)
get_filename_component(proto_path "${proto}" PATH)

set(out_dir "${CMAKE_SOURCE_DIR}/generated")
set(proto_src "${out_dir}/pubsub.pb.cc")
set(proto_hdr "${out_dir}/pubsub.pb.h")
set(grpc_src "${out_dir}/pubsub.grpc.pb.cc")
set(grpc_hdr "${out_dir}/pubsub.grpc.pb.h")

add_custom_command(
    OUTPUT "${proto_src}" "${proto_hdr}" "${grpc_src}" "${grpc_hdr}"
    COMMAND ${_PROTOBUF_PROTOC}
    ARGS --grpc_out "${out_dir}"
         --cpp_out "${out_dir}"
         -I "${proto_path}"
         --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
         "${proto}")

add_custom_target(protos DEPENDS ${proto_src} ${grpc_src})

# Include directories
include_directories(generated)
include_directories(lib)
include_directories(include)

# add_subdirectory(src)

add_executable(subscriber_client src/subscriber_client.cc ${proto_src} ${grpc_src})
target_link_libraries(subscriber_client grpc++ grpc++_reflection libprotobuf)
 
add_executable(publisher_client src/publisher_client.cc ${proto_src} ${grpc_src})
target_link_libraries(publisher_client grpc++ grpc++_reflection libprotobuf)

add_executable(broker_server src/broker_server.cc ${proto_src} ${grpc_src})
target_link_libraries(broker_server grpc++ grpc++_reflection libprotobuf)

add_custom_target(my_target)
add_dependencies(my_target protos broker_server publisher_client subscriber_client)