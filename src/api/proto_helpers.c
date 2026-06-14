
#include "esphome_api.h"
#include "esphome_api.pb.h"
#include "esphome_noise.h"
#include "esphome_transport.h"

#include <stdio.h>
#include <string.h>
#include <pb_encode.h>
#include <pb_decode.h>

int esph_frame_send(esph_session_t *s, const uint8_t *plaintext, size_t plen);
int esph_frame_recv(esph_session_t *s, uint8_t *out, size_t *out_len);


// Internal helper: encode a frame and send it
static int send_frame(esph_session_t *s, uint32_t type,
                      const uint8_t *payload, size_t plen)
{
    uint8_t buf[1024];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    if (!pb_encode_varint(&stream, type)) {
        fprintf(stderr, "[PROTO] Frame type encode failed\n");
        return -1;
    }

    if (stream.bytes_written + plen > sizeof(buf)) {
        fprintf(stderr, "[PROTO] Frame too large\n");
        return -1;
    }

    memcpy(buf + stream.bytes_written, payload, plen);
    size_t frame_len = stream.bytes_written + plen;
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

    return send_frame(s, 5 /*SubscribeStatesRequest*/, buf, stream.bytes_written);
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

    return send_frame(s, 21 /*SwitchCommandRequest*/, buf, stream.bytes_written);
}
