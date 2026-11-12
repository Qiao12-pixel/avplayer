add_library(avplayer_proto STATIC)
add_library(avplayer::proto ALIAS avplayer_proto)

set(AVPLAYER_PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/agent.proto")
set(AVPLAYER_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${AVPLAYER_PROTO_GEN_DIR}")

set(AVPLAYER_PROTO_SRCS "${AVPLAYER_PROTO_GEN_DIR}/agent.pb.cc")
set(AVPLAYER_PROTO_HDRS "${AVPLAYER_PROTO_GEN_DIR}/agent.pb.h")
set(AVPLAYER_GRPC_SRCS "${AVPLAYER_PROTO_GEN_DIR}/agent.grpc.pb.cc")
set(AVPLAYER_GRPC_HDRS "${AVPLAYER_PROTO_GEN_DIR}/agent.grpc.pb.h")

if(TARGET protobuf::protoc)
    set(AVPLAYER_PROTOC protobuf::protoc)
else()
    find_program(AVPLAYER_PROTOC protoc REQUIRED)
endif()

# Imported executable targets expose different location properties depending
# on the gRPC package (Homebrew vs Debian/Ubuntu).
set(AVPLAYER_GRPC_CPP_PLUGIN "")
foreach(AVPLAYER_PLUGIN_PROPERTY
        IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_NOCONFIG)
    get_target_property(AVPLAYER_PLUGIN_CANDIDATE gRPC::grpc_cpp_plugin ${AVPLAYER_PLUGIN_PROPERTY})
    if(AVPLAYER_PLUGIN_CANDIDATE AND NOT AVPLAYER_PLUGIN_CANDIDATE MATCHES "-NOTFOUND$")
        set(AVPLAYER_GRPC_CPP_PLUGIN "${AVPLAYER_PLUGIN_CANDIDATE}")
        break()
    endif()
endforeach()
if(NOT AVPLAYER_GRPC_CPP_PLUGIN)
    find_program(AVPLAYER_GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
endif()

add_custom_command(
    OUTPUT ${AVPLAYER_PROTO_SRCS} ${AVPLAYER_PROTO_HDRS} ${AVPLAYER_GRPC_SRCS} ${AVPLAYER_GRPC_HDRS}
    COMMAND ${AVPLAYER_PROTOC}
    ARGS --cpp_out "${AVPLAYER_PROTO_GEN_DIR}"
         --grpc_out "${AVPLAYER_PROTO_GEN_DIR}"
         --plugin=protoc-gen-grpc=${AVPLAYER_GRPC_CPP_PLUGIN}
         -I "${CMAKE_CURRENT_SOURCE_DIR}"
         "${AVPLAYER_PROTO_FILE}"
    DEPENDS "${AVPLAYER_PROTO_FILE}"
    COMMENT "Generating C++/gRPC stubs from proto/agent.proto"
    VERBATIM
)

target_sources(avplayer_proto PRIVATE
    ${AVPLAYER_PROTO_SRCS}
    ${AVPLAYER_PROTO_HDRS}
    ${AVPLAYER_GRPC_SRCS}
    ${AVPLAYER_GRPC_HDRS}
)
target_include_directories(avplayer_proto PUBLIC "${AVPLAYER_PROTO_GEN_DIR}")
target_link_libraries(avplayer_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)
