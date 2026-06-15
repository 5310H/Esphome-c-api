
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


/**
 * Internal helper to encode a frame, prepend the ESPHome unencrypted/inner header,
 * and send it securely via the session.
 *
 * @param s       Active session
 * @param type    The ESPH_MSG_* protobuf message type ID
 * @param payload The encoded protobuf payload
 * @param plen    Length of the payload
 * @return 0 on success, <0 on failure
 */
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
/**
 * Nanopb callback used to encode a string from a char pointer into a stream.
 */
static bool encode_string_cb(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
    const char *str = (const char *)*arg;
    if (!pb_encode_tag_for_field(stream, field)) return false;
    return pb_encode_string(stream, (uint8_t*)str, strlen(str));
}

int esph_send_hello(esph_session_t *s)
{
    uint8_t buf[256];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    HelloRequest msg = HelloRequest_init_zero;
    msg.client_info.funcs.encode = encode_string_cb;
    msg.client_info.arg = "esphome-c-api";
    msg.api_version_major = 1;
    msg.api_version_minor = 14;

    if (!pb_encode(&stream, HelloRequest_fields, &msg)) {
        fprintf(stderr, "[PROTO] HelloRequest encode failed\n");
        return -1;
    }

    return send_frame(s, 1 /*HelloRequest*/, buf, stream.bytes_written);
}

// ---------------------------------------------------------------------------
// ConnectRequest
// ---------------------------------------------------------------------------
// ConnectRequest is deprecated in Noise protocol. We no longer send it.

// ---------------------------------------------------------------------------
// DeviceInfoRequest
// ---------------------------------------------------------------------------
int esph_send_device_info_request(esph_session_t *s)
{
    uint8_t buf[32];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    DeviceInfoRequest msg =
        DeviceInfoRequest_init_zero;

    if (!pb_encode(&stream, DeviceInfoRequest_fields, &msg)) {
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

    ListEntitiesRequest msg = ListEntitiesRequest_init_zero;

    if (!pb_encode(&stream, ListEntitiesRequest_fields, &msg)) {
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

    SubscribeStatesRequest msg =
        SubscribeStatesRequest_init_zero;

    if (!pb_encode(&stream, SubscribeStatesRequest_fields, &msg)) {
        fprintf(stderr, "[PROTO] SubscribeStates encode failed\n");
        return -1;
    }

    return send_frame(s, 20 /*SubscribeStatesRequest*/, buf, stream.bytes_written);
}

// ---------------------------------------------------------------------------
// SwitchCommandRequest
// ---------------------------------------------------------------------------
/**
 * Send a SwitchCommandRequest to change the state of a switch entity.
 *
 * @param s     Active session
 * @param key   The numeric key of the entity to command
 * @param state 1 to turn on, 0 to turn off
 * @return 0 on success, <0 on failure
 */
int esph_send_switch_command(esph_session_t *s, uint32_t key, int state)
{
    uint8_t buf[32];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    SwitchCommandRequest msg =
        SwitchCommandRequest_init_zero;
    
    msg.key = key;
    msg.state = state ? true : false;

    if (!pb_encode(&stream, SwitchCommandRequest_fields, &msg)) {
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
    PingRequest msg = PingRequest_init_zero;
    if (!pb_encode(&stream, PingRequest_fields, &msg)) return -1;
    return send_frame(s, 7 /*PingRequest*/, buf, stream.bytes_written);
}

int esph_send_ping_response(esph_session_t *s)
{
    uint8_t buf[16];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    PingResponse msg = PingResponse_init_zero;
    if (!pb_encode(&stream, PingResponse_fields, &msg)) return -1;
    return send_frame(s, 8 /*PingResponse*/, buf, stream.bytes_written);
}
