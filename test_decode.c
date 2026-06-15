#include "esphome_api.h"
#include "esphome_api.pb.h"
#include <stdio.h>
#include <pb_decode.h>

static bool decode_string_cb(pb_istream_t *stream, const pb_field_t *field, void **arg) {
    char *buffer = (char*)*arg;
    size_t len = stream->bytes_left;
    printf("[DEBUG] Callback invoked! bytes_left=%zu\n", len);
    if (len > 255) len = 255;
    if (!pb_read(stream, (uint8_t*)buffer, len)) {
        return false;
    }
    buffer[len] = '\0';
    return true;
}

int main() {
    // Hex payload: 0d d5 90 57 5a 12 0e 31 39 32 2e 31 36 38 2e 36 39 2e 31 33 36
    uint8_t buf[] = {0x0d, 0xd5, 0x90, 0x57, 0x5a, 0x12, 0x0e, 0x31, 0x39, 0x32, 0x2e, 0x31, 0x36, 0x38, 0x2e, 0x36, 0x39, 0x2e, 0x31, 0x33, 0x36};
    
    pb_istream_t stream = pb_istream_from_buffer(buf, sizeof(buf));
    esphome_api_TextSensorStateResponse msg = esphome_api_TextSensorStateResponse_init_zero;
    char text_buf[256] = {0};
    
    msg.state.funcs.decode = decode_string_cb;
    msg.state.arg = text_buf;
    
    if (pb_decode(&stream, esphome_api_TextSensorStateResponse_fields, &msg)) {
        printf("Decode SUCCESS\n");
        printf("Key: %u\n", msg.key);
        printf("State: %s\n", text_buf);
    } else {
        printf("Decode FAILED: %s\n", PB_GET_ERROR(&stream));
    }
    return 0;
}
