
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
 * Helper function: Encodes a given Nanopb message struct into a byte buffer 
 * and sends it over the active session as an encrypted frame.
 *
 * @param s         The active ESPHome session (holds the socket and encryption context)
 * @param type      The numeric ID of the message type (e.g. 1 for HelloRequest)
 * @param fields    The Nanopb fields descriptor array for this message type
 * @param msg       A pointer to the actual message struct to encode
 * @return 0 on success, <0 on failure
 */
static int send_frame(esph_session_t *s, uint32_t type, const pb_msgdesc_t *fields, const void *msg)
{
    uint8_t buf[1024];

    // Initialize the Nanopb output stream pointing into our buffer.
    // We reserve the first 4 bytes for the plaintext preamble and length.
    pb_ostream_t stream = pb_ostream_from_buffer(buf + 4, sizeof(buf) - 4);
    
    // Encode the struct into the protobuf binary format
    if (!pb_encode(&stream, fields, msg)) {
        return -1;
    }
    
    size_t plen = stream.bytes_written;

    // The Native API protocol requires a 4-byte unencrypted preamble inside the plaintext 
    // payload BEFORE encryption.
    // Byte 0: `0x00`
    // Byte 1: The message type (varint encoded, assuming < 128 for simple messages)
    // Byte 2 & 3: The length of the protobuf payload (big-endian)
    buf[0] = 0x00;
    buf[1] = type & 0xFF; // Simplification: assuming type < 128
    buf[2] = (plen >> 8) & 0xFF;
    buf[3] = plen & 0xFF;

    return esph_frame_send(s, buf, 4 + plen);
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
    HelloRequest msg = HelloRequest_init_zero;
    msg.client_info.funcs.encode = encode_string_cb;
    msg.client_info.arg = "esphome-c-api";
    msg.api_version_major = 1;
    msg.api_version_minor = 14;

    return send_frame(s, 1, HelloRequest_fields, &msg);
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
    DeviceInfoRequest msg = DeviceInfoRequest_init_zero;
    return send_frame(s, 9, DeviceInfoRequest_fields, &msg);
}

// ---------------------------------------------------------------------------
// ListEntitiesRequest
// ---------------------------------------------------------------------------
int esph_send_list_entities(esph_session_t *s)
{
    ListEntitiesRequest msg = ListEntitiesRequest_init_zero;
    return send_frame(s, 11, ListEntitiesRequest_fields, &msg);
}

// ---------------------------------------------------------------------------
// SubscribeStatesRequest
// ---------------------------------------------------------------------------
int esph_send_subscribe_states(esph_session_t *s)
{
    SubscribeStatesRequest msg = SubscribeStatesRequest_init_zero;
    return send_frame(s, 20, SubscribeStatesRequest_fields, &msg);
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
    SwitchCommandRequest msg = SwitchCommandRequest_init_zero;
    
    msg.key = key;
    msg.state = state ? true : false;

    return send_frame(s, 33, SwitchCommandRequest_fields, &msg);
}

// ---------------------------------------------------------------------------
// PingRequest / Response
// ---------------------------------------------------------------------------
int esph_send_ping_request(esph_session_t *s)
{
    PingRequest msg = PingRequest_init_zero;
    return send_frame(s, 7, PingRequest_fields, &msg);
}

// ---------------------------------------------------------------------------
// PingResponse (Sent in response to PingRequest from server)
// ---------------------------------------------------------------------------
int esph_send_ping_response(esph_session_t *s)
{
    PingResponse msg = PingResponse_init_zero;
    return send_frame(s, 8, PingResponse_fields, &msg);
}
