#include "esphome_api.h"
#include "esphome_noise.h"
#include "esphome_transport.h"
#include "esphome_api.pb.h"
#include "esphome_messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pb_encode.h>
#include <pb_decode.h>

#if defined(ESP_PLATFORM) || defined(ESP8266)
// ESP-IDF / ESP8266 RTOS SDK using LwIP
#include <lwip/sockets.h>
#include <unistd.h>
#elif defined(_WIN32)
// Windows
#include <winsock2.h>
#define close closesocket
#else
// Standard POSIX (Linux, macOS, etc.)
#include <unistd.h>
#endif


/**
 * The internal session state structure for the ESPHome C API.
 * Keeps track of the socket, Noise encryption context, and iteration state.
 */
struct esph_session {
    int sock;                       // TCP socket descriptor
    esph_noise_ctx_t *noise;        // Noise protocol encryption context
    bool list_entities_done;        // Flag indicating if ListEntities iteration has finished
};

// From frame.c
int esph_frame_send(esph_session_t *s, const uint8_t *plaintext, size_t plen);
int esph_frame_recv(esph_session_t *s, uint32_t *type, uint8_t *out, size_t *out_len);
int esph_frame_wait_readable(esph_session_t *s, int timeout_ms);



// From proto_helpers.c
int esph_send_hello(esph_session_t *s);
int esph_send_device_info_request(esph_session_t *s);
int esph_send_subscribe_states(esph_session_t *s);
int esph_send_switch_command(esph_session_t *s, uint32_t key, int state);
int esph_send_ping_request(esph_session_t *s);
int esph_send_ping_response(esph_session_t *s);

// ---------------------------------------------------------------------------
// Create + connect + Noise handshake + Hello + Connect
// ---------------------------------------------------------------------------
esph_session_t *esph_connect(const char *host, uint16_t port, const char *psk)
{
    esph_session_t *s = calloc(1, sizeof(esph_session_t));
    if (!s) {
        fprintf(stderr, "[API] alloc failed\n");
        return NULL;
    }

    // 1. TCP connect: Establishes a raw TCP socket connection to the ESPHome device (typically port 6053).
    printf("[API] Step 1: Connecting TCP to %s:%u...\n", host, (unsigned)port);
    s->sock = esph_transport_connect(host, port);
    if (s->sock < 0) {
        fprintf(stderr, "[API] Step 1 FAILED: TCP connect to %s:%u failed\n", host, (unsigned)port);
        free(s);
        return NULL;
    }
    printf("[API] Step 1 SUCCESS: TCP connected (sock=%d)\n", s->sock);

    if (psk && strlen(psk) > 0) {
        // 2. Noise init
        printf("[API] Step 2: Initializing Noise crypto with PSK...\n");
        if (esph_noise_init(&s->noise, psk) != 0) {
            fprintf(stderr, "[API] Step 2 FAILED: Noise init failed (invalid PSK?)\n");
            close(s->sock);
            free(s);
            return NULL;
        }
        printf("[API] Step 2 SUCCESS: Noise crypto initialized\n");

        // 3. Noise handshake
        printf("[API] Step 3: Executing Noise handshake with %s:%u...\n", host, (unsigned)port);
        if (esph_noise_handshake(s->noise, s->sock) != 0) {
            fprintf(stderr, "[API] Step 3 FAILED: Noise handshake rejected by %s:%u\n", host, (unsigned)port);
            esph_disconnect(s);
            free(s);
            return NULL;
        }
        printf("[API] Step 3 SUCCESS: Noise handshake completed and ciphers active\n");
    } else {
        printf("[API] Skipping Noise (plaintext API mode)\n");
    }

    // 4. Send HelloRequest
    printf("[API] Step 4: Sending HelloRequest to %s:%u...\n", host, (unsigned)port);
    if (esph_send_hello(s) != 0) {
        fprintf(stderr, "[API] Step 4 FAILED: HelloRequest send failed\n");
        esph_disconnect(s);
        free(s);
        return NULL;
    }
    printf("[API] Step 4 SUCCESS: HelloRequest sent\n");

    // 5. Receive HelloResponse
    printf("[API] Step 5: Waiting for HelloResponse from %s:%u...\n", host, (unsigned)port);
    {
        uint8_t buf[512];
        size_t len = sizeof(buf);

        uint32_t msg_type = 0;
        if (esph_frame_recv(s, &msg_type, buf, &len) != 0) {
            fprintf(stderr, "[API] Step 5 FAILED: HelloResponse receive failed\n");
            esph_disconnect(s);
            free(s);
            return NULL;
        }
        printf("[API] Received frame (type=%u, len=%zu)\n", (unsigned)msg_type, len);

        pb_istream_t stream = pb_istream_from_buffer(buf, len);

        if (msg_type != ESPH_MSG_HELLO_RESPONSE) {
            fprintf(stderr, "[API] Step 5 FAILED: Expected HelloResponse (2), got type %u\n", (unsigned)msg_type);
            esph_disconnect(s);
            free(s);
            return NULL;
        }

        HelloResponse resp = HelloResponse_init_zero;
        if (!pb_decode(&stream, HelloResponse_fields, &resp)) {
            fprintf(stderr, "[API] Step 5 FAILED: HelloResponse decode failed\n");
            esph_disconnect(s);
            free(s);
            return NULL;
        }
        printf("[API] Step 5 SUCCESS: Connected to ESPHome node (API version %u.%u)\n",
               (unsigned)resp.api_version_major, (unsigned)resp.api_version_minor);
    }

    return s;
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------
int esph_disconnect(esph_session_t *s) {
    if (!s) return -1;
    if (s->sock >= 0) {
        close(s->sock);
        s->sock = -1;
    }
    if (s->noise) {
        esph_noise_free(s->noise);
        s->noise = NULL;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Check device info
// ---------------------------------------------------------------------------
int esph_check_device_info(esph_session_t *s)
{
    if (esph_send_device_info_request(s) != 0) {
        return -1;
    }

    uint8_t buf[512];
    size_t len = sizeof(buf);
    
    uint32_t msg_type = 0;
    if (esph_frame_recv(s, &msg_type, buf, &len) != 0) {
        fprintf(stderr, "[API] DeviceInfoResponse recv failed\n");
        return -1;
    }

    pb_istream_t stream = pb_istream_from_buffer(buf, len);

    if (msg_type != 10 /* ESPH_MSG_DEVICE_INFO_RESPONSE */) {
        fprintf(stderr, "[API] Unexpected frame type %u, expected 10\n", (unsigned)msg_type);
        return -1;
    }

    DeviceInfoResponse resp = DeviceInfoResponse_init_zero;

    if (!pb_decode(&stream, DeviceInfoResponse_fields, &resp)) {
        fprintf(stderr, "[API] DeviceInfoResponse decode failed\n");
        return -1;
    }

    printf("--- Device Info ---\n");
    printf("Connected successfully.\n");
    printf("-------------------\n");

    return 0;
}


// ---------------------------------------------------------------------------
// Subscribe to states
// ---------------------------------------------------------------------------
int esph_subscribe_states(esph_session_t *s)
{
    return esph_send_subscribe_states(s);
}

// ---------------------------------------------------------------------------
// Wait for List Entities Done
// ---------------------------------------------------------------------------
int esph_wait_list_entities_done(esph_session_t *s)
{
    int max_steps = 25;
    int step = 0;
    while (!s->list_entities_done && step++ < max_steps) {
        int ret = esph_run_step(s, 150);
        if (ret < 0) {
            fprintf(stderr, "[API] Error in esph_run_step during entity discovery (step %d)\n", step);
            return -1;
        }
    }
    if (s->list_entities_done) {
        printf("[API] Successfully received ListEntitiesDone marker\n");
        return 0;
    }
    fprintf(stderr, "[API] Timeout: Did not receive ListEntitiesDone within %d ms\n", max_steps * 150);
    return -1;
}

// ---------------------------------------------------------------------------
// Switch command
// ---------------------------------------------------------------------------
extern uint32_t esph_registry_lookup_key(const char *entity_id);

int esph_set_switch(esph_session_t *s, const char *entity_id, int state)
{
    uint32_t key = esph_registry_lookup_key(entity_id);
    if (key == 0) {
        fprintf(stderr, "[API] Error: entity_id '%s' not found in registry\n", entity_id);
        return -1;
    }
    return esph_send_switch_command(s, key, state);
}

// ---------------------------------------------------------------------------
// Run loop step
// ---------------------------------------------------------------------------

/**
 * A Nanopb callback function to decode strings directly into a pre-allocated buffer.
 * It is used for parsing `object_id` and `name` strings from ListEntities responses.
 */
static bool decode_string_cb(pb_istream_t *stream, const pb_field_t *field, void **arg) {
    char *buffer = (char*)*arg;
    size_t len = stream->bytes_left;
    if (len > 255) len = 255;
    
    if (!pb_read(stream, (uint8_t*)buffer, len)) {
        return false;
    }
    buffer[len] = '\0';
    return true;
}

/**
 * Read and process exactly one incoming frame from the ESPHome device.
 * It handles state updates (sensors, switches) and populates the entity registry
 * with IDs during the initial setup phase.
 */
int esph_run_step(esph_session_t *s, int timeout_ms)
{
    // Wait for the socket to become readable.
    // If timeout_ms is 0, this instantly checks and returns.
    // If timeout_ms is > 0, it blocks up to that many milliseconds.
    int ret = esph_frame_wait_readable(s, timeout_ms);
    if (ret < 0) return -1; // Socket error or disconnected
    if (ret == 0) return 0; // Timeout reached, no data arrived in this step


    uint8_t buf[512];
    size_t len = sizeof(buf);
    uint32_t msg_type = 0;

    extern void esph_registry_add(const char *object_id, uint32_t key, uint32_t legacy_type);
    extern void esph_registry_update_state(uint32_t key, const char *state_str);

    // Read the next complete frame from the socket.
    // esph_frame_recv handles reading the 3-byte header, reading the payload, and decrypting it using the Noise context.
    if (esph_frame_recv(s, &msg_type, buf, &len) != 0) {
        return -1;
    }

    // Initialize a Nanopb stream to decode the decrypted protobuf payload.
    pb_istream_t stream = pb_istream_from_buffer(buf, len);

    if (msg_type == 7) { // PingRequest
        return esph_send_ping_response(s);
    } else if (msg_type == 8) { // PingResponse
        printf("[API] Received PingResponse\n");
        return 0; // Handled
    } else if (msg_type == 5) { // DisconnectRequest
        return -1;
    } else if (msg_type == ESPH_MSG_LIST_ENTITIES_BINARY_SENSOR_RESPONSE) {
        ListEntitiesBinarySensorResponse msg = ListEntitiesBinarySensorResponse_init_zero;
        char object_id[128] = {0};
        char entity_name[128] = {0};
        msg.object_id.funcs.decode = decode_string_cb;
        msg.object_id.arg = object_id;
        msg.name.funcs.decode = decode_string_cb;
        msg.name.arg = entity_name;
        // The decode_string_cb callback automatically extracts variable-length string fields from the payload.
        if (pb_decode(&stream, ListEntitiesBinarySensorResponse_fields, &msg)) {
            // Register the entity dynamically. If the `object_id` (the short name like "smart_plug_status")
            // is missing, we fall back to using the `name` field (the friendly name like "Smart Plug Status").
            esph_registry_add(object_id[0] ? object_id : entity_name, msg.key, msg_type);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type %u: %s\n", (unsigned)msg_type, PB_GET_ERROR(&stream));
        }
    } else if (msg_type == ESPH_MSG_LIST_ENTITIES_SWITCH_RESPONSE) {
        ListEntitiesSwitchResponse msg = ListEntitiesSwitchResponse_init_zero;
        char object_id[128] = {0};
        char entity_name[128] = {0};
        msg.object_id.funcs.decode = decode_string_cb;
        msg.object_id.arg = object_id;
        msg.name.funcs.decode = decode_string_cb;
        msg.name.arg = entity_name;
        if (pb_decode(&stream, ListEntitiesSwitchResponse_fields, &msg)) {
            // Register the entity, allowing us to send commands to it later by referencing its string name.
            esph_registry_add(object_id[0] ? object_id : entity_name, msg.key, msg_type);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type %u: %s\n", (unsigned)msg_type, PB_GET_ERROR(&stream));
        }
    } else if (msg_type == ESPH_MSG_LIST_ENTITIES_SENSOR_RESPONSE) {
        ListEntitiesSensorResponse msg = ListEntitiesSensorResponse_init_zero;
        char object_id[128] = {0};
        char entity_name[128] = {0};
        msg.object_id.funcs.decode = decode_string_cb;
        msg.object_id.arg = object_id;
        msg.name.funcs.decode = decode_string_cb;
        msg.name.arg = entity_name;
        if (pb_decode(&stream, ListEntitiesSensorResponse_fields, &msg)) {
            // printf("[API] Found Sensor: object_id='%s', name='%s' (key=%u)\n", object_id, entity_name, msg.key);
            esph_registry_add(object_id[0] ? object_id : entity_name, msg.key, msg_type);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type %u: %s\n", (unsigned)msg_type, PB_GET_ERROR(&stream));
        }
    } else if (msg_type == ESPH_MSG_LIST_ENTITIES_TEXT_SENSOR_RESPONSE) {
        ListEntitiesTextSensorResponse msg = ListEntitiesTextSensorResponse_init_zero;
        char object_id[128] = {0};
        char entity_name[128] = {0};
        msg.object_id.funcs.decode = decode_string_cb;
        msg.object_id.arg = object_id;
        msg.name.funcs.decode = decode_string_cb;
        msg.name.arg = entity_name;
        if (pb_decode(&stream, ListEntitiesTextSensorResponse_fields, &msg)) {
            // printf("[API] Found TextSensor: object_id='%s', name='%s' (key=%u)\n", object_id, entity_name, msg.key);
            esph_registry_add(object_id[0] ? object_id : entity_name, msg.key, msg_type);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type %u: %s\n", (unsigned)msg_type, PB_GET_ERROR(&stream));
        }
    } else if (msg_type == ESPH_MSG_LIST_ENTITIES_DONE_RESPONSE) {
        // This message signifies that the device has finished transmitting the full list of available entities.
        s->list_entities_done = true;
    } else if (msg_type == 21) { // BinarySensorStateResponse
        // The device pushed a state update for a binary sensor.
        BinarySensorStateResponse msg = BinarySensorStateResponse_init_zero;
        if (pb_decode(&stream, BinarySensorStateResponse_fields, &msg)) {
            // Save the newly received state ("ON" or "OFF") into the global registry cache.
            esph_registry_update_state(msg.key, msg.state ? "ON" : "OFF");
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 21: %s\n", PB_GET_ERROR(&stream));
        }
    } else if (msg_type == 26) { // SwitchStateResponse
        SwitchStateResponse msg = SwitchStateResponse_init_zero;
        if (pb_decode(&stream, SwitchStateResponse_fields, &msg)) {
            esph_registry_update_state(msg.key, msg.state ? "ON" : "OFF");
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 26: %s\n", PB_GET_ERROR(&stream));
        }
    } else if (msg_type == 25) { // SensorStateResponse
        SensorStateResponse msg = SensorStateResponse_init_zero;
        if (pb_decode(&stream, SensorStateResponse_fields, &msg)) {
            char val_buf[32];
            snprintf(val_buf, sizeof(val_buf), "%.2f", msg.state);
            esph_registry_update_state(msg.key, val_buf);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 25: %s\n", PB_GET_ERROR(&stream));
        }
    } else if (msg_type == 27) { // TextSensorStateResponse
        TextSensorStateResponse msg = TextSensorStateResponse_init_zero;
        char text_buf[256] = {0};
        msg.state.funcs.decode = decode_string_cb;
        msg.state.arg = text_buf;
        
        if (pb_decode(&stream, TextSensorStateResponse_fields, &msg)) {
            esph_registry_update_state(msg.key, text_buf);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 27: %s\n", PB_GET_ERROR(&stream));
        }
    } else {
        // Unhandled msg type
    }

    return 0;
}
