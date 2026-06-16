cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS 
        "src/api/esphome_api.c"
        "src/api/frame.c"
        "src/api/proto_helpers.c"
        "src/noise/noise.c"
        "src/proto/api.pb.c"
        "src/proto/api_options.pb.c"
        "src/proto/pb_common.c"
        "src/proto/pb_decode.c"
        "src/proto/pb_encode.c"
        "src/protobuf/messages.c"
        "src/transport/transport.c"
    INCLUDE_DIRS "include" "src/proto"
    REQUIRES lwip noise-c
)
