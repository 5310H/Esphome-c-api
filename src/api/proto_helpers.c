
#include "esphome_api.h"
#include "esphome_api.pb.h"
#include "esphome_noise.h"
#include "esphome_transport.h"

#include <stdio.h>
#include <string.h>
#include <pb_encode.h>
#include <pb_decode.h>

int esph_frame_send(esph_session_t *s, const uint8_t *plaintext, size_t plen);
int esph_frame_recv(esph_session_t *s, uint32_t *type, uint8_t *out, size_t *out_len);


// Internal helper: encode a frame and send it
static int send_frame(esph_session_t *s, uint32_t type,
                      const uint8_t *payload, size_t plen)
{
    uint8_t buf[1024];
    // For Noise protocol, the inner header is 2 bytes type (big-endian), 2 bytes length (big-endian)
    buf[0] = (type >> 8) & 0xFF;
    buf[1] = type & 0xFF;
    buf[2] = (plen >> 8) & 0xFF;
    buf[3] = plen & 0xFF;

    if (4 + plen > sizeof(buf)) {
        fprintf(stderr, "[PROTO] Frame too large\n");
        return -1;
    }

    memcpy(buf + 4, payload, plen);
    size_t frame_len = 4 + plen;

    fprintf(stderr, "[PROTO] Sending frame type=%u, payload_len=%zu\n", type, plen);
    fprintf(stderr, "[PROTO] Hex: ");
    for (size_t i = 0; i < frame_len; i++) {
        fprintf(stderr, "%02x ", buf[i]);
    }
    fprintf(stderr, "\n");

    return esph_frame_send(s, buf, frame_len);
}

// ---------------------------------------------------------------------------
// HelloRequest
// ---------------------------------------------------------------------------
int esph_send_hello(esph_session_t *s)
{
    uint8_t buf[256];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    esphome_api_HelloRequest msg = esphome_api_HelloRequest_init_zero;
    strncpy(msg.client_info, "esphome-c-api", sizeof(msg.client_info)-1);

    if (!pb_encode(&stream, esphome_api_HelloRequest_fields, &msg)) {
        fprintf(stderr, "[PROTO] HelloRequest encode failed\n");
        return -1;
    }

    return send_frame(s, 1 /*HelloRequest*/, buf, stream.bytes_written);
}

// ---------------------------------------------------------------------------
// ConnectRequest
// ---------------------------------------------------------------------------
int esph_send_connect(esph_session_t *s, const char *password)
{
    uint8_t buf[256];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    esphome_api_ConnectRequest msg = esphome_api_ConnectRequest_init_zero;
    strncpy(msg.password, password, sizeof(msg.password)-1);

    if (!pb_encode(&stream, esphome_api_ConnectRequest_fields, &msg)) {
        fprintf(stderr, "[PROTO] ConnectRequest encode failed\n");
        return -1;
    }

    return send_frame(s, 3 /*ConnectRequest*/, buf, stream.bytes_written);
}

// ---------------------------------------------------------------------------
// DeviceInfoRequest
// ---------------------------------------------------------------------------
int esph_send_device_info_request(esph_session_t *s)
{
    uint8_t buf[32];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    esphome_api_DeviceInfoRequest msg =
        esphome_api_DeviceInfoRequest_init_zero;

    if (!pb_encode(&stream, esphome_api_DeviceInfoRequest_fields, &msg)) {
        fprintf(stderr, "[PROTO] DeviceInfoRequest encode failed\n");
        return -1;
    }

    return send_frame(s, 9 /*DeviceInfoRequest*/, buf, stream.bytes_written);
}

// ---------------------------------------------------------------------------
// ListEntitiesRequest
// ---------------------------------------------------------------------------
int esph_send_list_entities(esph_session_t *s)
{
    uint8_t buf[32];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    esphome_api_ListEntitiesRequest msg = esphome_api_ListEntitiesRequest_init_zero;

    if (!pb_encode(&stream, esphome_api_ListEntitiesRequest_fields, &msg)) {
        fprintf(stderr, "[PROTO] ListEntitiesRequest encode failed\n");
        return -1;
    }

    return send_frame(s, 11 /*ListEntitiesRequest*/, buf, stream.bytes_written);
}

// ---------------------------------------------------------------------------
// SubscribeStatesRequest
// ---------------------------------------------------------------------------
int esph_send_subscribe_states(esph_session_t *s)
{
    uint8_t buf[64];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    esphome_api_SubscribeStatesRequest msg =
        esphome_api_SubscribeStatesRequest_init_zero;

    if (!pb_encode(&stream, esphome_api_SubscribeStatesRequest_fields, &msg)) {
        fprintf(stderr, "[PROTO] SubscribeStates encode failed\n");
        return -1;
    }

    return send_frame(s, 20 /*SubscribeStatesRequest*/, buf, stream.bytes_written);
}

// ---------------------------------------------------------------------------
// SwitchCommandRequest
// ---------------------------------------------------------------------------
int esph_send_switch_command(esph_session_t *s, uint32_t key, int state)
{
    uint8_t buf[128];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    esphome_api_SwitchCommandRequest msg =
        esphome_api_SwitchCommandRequest_init_zero;

    msg.key = key;
    msg.state = state ? true : false;

    if (!pb_encode(&stream, esphome_api_SwitchCommandRequest_fields, &msg)) {
        fprintf(stderr, "[PROTO] SwitchCommand encode failed\n");
        return -1;
    }

    return send_frame(s, 33 /*SwitchCommandRequest*/, buf, stream.bytes_written);
}

// ---------------------------------------------------------------------------
// PingRequest / Response
// ---------------------------------------------------------------------------
int esph_send_ping_request(esph_session_t *s)
{
    uint8_t buf[16];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    esphome_api_PingRequest msg = esphome_api_PingRequest_init_zero;
    if (!pb_encode(&stream, esphome_api_PingRequest_fields, &msg)) return -1;
    return send_frame(s, 7 /*PingRequest*/, buf, stream.bytes_written);
}

int esph_send_ping_response(esph_session_t *s)
{
    uint8_t buf[16];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    esphome_api_PingResponse msg = esphome_api_PingResponse_init_zero;
    if (!pb_encode(&stream, esphome_api_PingResponse_fields, &msg)) return -1;
    return send_frame(s, 8 /*PingResponse*/, buf, stream.bytes_written);
}
