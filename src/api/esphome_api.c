#include "esphome_api.h"
#include "esphome_noise.h"
#include "esphome_transport.h"
#include "esphome_api.pb.h"
#include "esphome_messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    // 1. TCP connect
    s->sock = esph_transport_connect(host, port);
    if (s->sock < 0) {
        fprintf(stderr, "[API] TCP connect failed\n");
        free(s);
        return NULL;
    }

    if (psk && strlen(psk) > 0) {
        // 2. Noise init
        if (esph_noise_init(&s->noise, psk) != 0) {
            fprintf(stderr, "[API] Noise init failed\n");
            close(s->sock);
            free(s);
            return NULL;
        }

        // 3. Noise handshake
        if (esph_noise_handshake(s->noise, s->sock) != 0) {
            fprintf(stderr, "[API] Noise handshake failed\n");
            esph_disconnect(s);
            free(s);
            return NULL;
        }
    }


    // 4. Send HelloRequest
    if (esph_send_hello(s) != 0) {
        fprintf(stderr, "[API] HelloRequest failed\n");
        esph_disconnect(s);
        free(s);
        return NULL;
    }

    // 5. Receive HelloResponse
    {
        uint8_t buf[512];
        size_t len = sizeof(buf);

        uint32_t msg_type = 0;
        printf("[API] Waiting for HelloResponse...\n"); fflush(stdout);
        if (esph_frame_recv(s, &msg_type, buf, &len) != 0) {
            fprintf(stderr, "[API] HelloResponse recv failed\n"); fflush(stderr);
            esph_disconnect(s);
            free(s);
            return NULL;
        }
        printf("[API] Received HelloResponse\n"); fflush(stdout);

        pb_istream_t stream = pb_istream_from_buffer(buf, len);

        if (msg_type != ESPH_MSG_HELLO_RESPONSE) {
            fprintf(stderr, "[API] Unexpected frame type %u\n", (uint32_t)msg_type);
            esph_disconnect(s);
            free(s);
            return NULL;
        }

        HelloResponse resp = HelloResponse_init_zero;
        if (!pb_decode(&stream, HelloResponse_fields, &resp)) {
            fprintf(stderr, "[API] HelloResponse decode failed\n");
            esph_disconnect(s);
            free(s);
            return NULL;
        }
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
        fprintf(stderr, "[API] Unexpected frame type %u, expected 10\n", (uint32_t)msg_type);
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
    while (!s->list_entities_done) {
        if (esph_run_step(s, 1000) < 0) {
            return -1;
        }
    }
    return 0;
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
    int ret = esph_frame_wait_readable(s, timeout_ms);
    if (ret < 0) return -1;
    if (ret == 0) return 0; // Timeout, no data to process

    uint8_t buf[2048];
    size_t len = sizeof(buf);
    uint32_t msg_type = 0;

    extern void esph_registry_add(const char *object_id, uint32_t key, uint32_t legacy_type);

    if (esph_frame_recv(s, &msg_type, buf, &len) != 0) {
        return -1;
    }

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
        if (pb_decode(&stream, ListEntitiesBinarySensorResponse_fields, &msg)) {
            printf("[API] Found BinarySensor: object_id='%s', name='%s' (key=%u)\n", object_id, entity_name, msg.key);
            esph_registry_add(object_id[0] ? object_id : entity_name, msg.key, msg_type);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type %u: %s\n", msg_type, PB_GET_ERROR(&stream));
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
            printf("[API] Found Switch: object_id='%s', name='%s' (key=%u)\n", object_id, entity_name, msg.key);
            esph_registry_add(object_id[0] ? object_id : entity_name, msg.key, msg_type);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type %u: %s\n", msg_type, PB_GET_ERROR(&stream));
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
            printf("[API] Found Sensor: object_id='%s', name='%s' (key=%u)\n", object_id, entity_name, msg.key);
            esph_registry_add(object_id[0] ? object_id : entity_name, msg.key, msg_type);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type %u: %s\n", msg_type, PB_GET_ERROR(&stream));
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
            printf("[API] Found TextSensor: object_id='%s', name='%s' (key=%u)\n", object_id, entity_name, msg.key);
            esph_registry_add(object_id[0] ? object_id : entity_name, msg.key, msg_type);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type %u: %s\n", msg_type, PB_GET_ERROR(&stream));
        }
    } else if (msg_type == ESPH_MSG_LIST_ENTITIES_DONE_RESPONSE) {
        printf("[API] ListEntities iteration complete.\n");
        s->list_entities_done = true;
    } else if (msg_type == 21) { // BinarySensorStateResponse
        BinarySensorStateResponse msg = BinarySensorStateResponse_init_zero;
        if (pb_decode(&stream, BinarySensorStateResponse_fields, &msg)) {
            printf("[STATE] BinarySensor key=%u state=%s missing=%d\n", msg.key, msg.state ? "ON" : "OFF", msg.missing_state);
            fflush(stdout);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 21: %s\n", PB_GET_ERROR(&stream));
        }
    } else if (msg_type == 26) { // SwitchStateResponse
        SwitchStateResponse msg = SwitchStateResponse_init_zero;
        if (pb_decode(&stream, SwitchStateResponse_fields, &msg)) {
            printf("[STATE] Switch key=%u state=%s\n", msg.key, msg.state ? "ON" : "OFF");
            fflush(stdout);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 26: %s\n", PB_GET_ERROR(&stream));
        }
    } else if (msg_type == 25) { // SensorStateResponse
        SensorStateResponse msg = SensorStateResponse_init_zero;
        if (pb_decode(&stream, SensorStateResponse_fields, &msg)) {
            printf("[STATE] Sensor key=%u state=%.2f missing=%d\n", msg.key, msg.state, msg.missing_state);
            fflush(stdout);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 25: %s\n", PB_GET_ERROR(&stream));
        }
    } else if (msg_type == 27) { // TextSensorStateResponse
        TextSensorStateResponse msg = TextSensorStateResponse_init_zero;
        char text_buf[256] = {0};
        msg.state.funcs.decode = decode_string_cb;
        msg.state.arg = text_buf;
        
        if (pb_decode(&stream, TextSensorStateResponse_fields, &msg)) {
            printf("[STATE] TextSensor key=%u state=%s missing=%d\n", msg.key, text_buf, msg.missing_state);
            fflush(stdout);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 27: %s\n", PB_GET_ERROR(&stream));
        }
    } else {
        printf("[API] Unhandled message type: %u\n", msg_type);
        fflush(stdout);
    }

    return 0;
}
