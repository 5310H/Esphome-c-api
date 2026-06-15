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

#ifdef _WIN32
#include <winsock2.h>
#define close closesocket
#else
#include <unistd.h>
#endif


// Define the opaque structure esph_session here so that esphome_api.c can access its members
struct esph_session {
    int sock;
    esph_noise_ctx_t *noise;
};

// From frame.c
int esph_frame_send(esph_session_t *s, const uint8_t *plaintext, size_t plen);
int esph_frame_recv(esph_session_t *s, uint32_t *type, uint8_t *out, size_t *out_len);
int esph_frame_wait_readable(esph_session_t *s, int timeout_ms);



// From proto_helpers.c
int esph_send_hello(esph_session_t *s);
int esph_send_connect(esph_session_t *s, const char *password);
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

        esphome_api_HelloResponse resp = esphome_api_HelloResponse_init_zero;
        if (!pb_decode(&stream, esphome_api_HelloResponse_fields, &resp)) {
            fprintf(stderr, "[API] HelloResponse decode failed\n");
            esph_disconnect(s);
            free(s);
            return NULL;
        }
    }

    // 6. Send ConnectRequest
    if (esph_send_connect(s, "") != 0) {
        fprintf(stderr, "[API] ConnectRequest failed\n");
        esph_disconnect(s);
        free(s);
        return NULL;
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

    esphome_api_DeviceInfoResponse resp = esphome_api_DeviceInfoResponse_init_zero;

    if (!pb_decode(&stream, esphome_api_DeviceInfoResponse_fields, &resp)) {
        fprintf(stderr, "[API] DeviceInfoResponse decode failed\n");
        return -1;
    }

    printf("--- Device Info ---\n");
    printf("Name:            %s\n", resp.name);
    printf("MAC:             %s\n", resp.mac_address);
    printf("ESPHome Version: %s\n", resp.esphome_version);
    printf("Compiled:        %s\n", resp.compilation_time);
    printf("Model:           %s\n", resp.model);
    printf("Project:         %s v%s\n", resp.project_name, resp.project_version);
    printf("Manufacturer:    %s\n", resp.manufacturer);
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
    uint8_t buf[2048];
    size_t len;
    uint32_t msg_type = 0;

    while (1) {
        len = sizeof(buf);
        if (esph_frame_recv(s, &msg_type, buf, &len) != 0) {
            return -1;
        }
        if (msg_type == 19) { // ListEntitiesDoneResponse
            break;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Switch command
// ---------------------------------------------------------------------------
int esph_set_switch(esph_session_t *s, const char *entity_id, int state)
{
    // TODO: entity_id → key lookup
    uint32_t fake_key = 1;
    return esph_send_switch_command(s, fake_key, state);
}

// ---------------------------------------------------------------------------
// Run loop step
// ---------------------------------------------------------------------------
int esph_run_step(esph_session_t *s, int timeout_ms)
{
    int ret = esph_frame_wait_readable(s, timeout_ms);
    if (ret < 0) return -1;
    if (ret == 0) return 0; // Timeout, no data to process

    uint8_t buf[2048];
    size_t len = sizeof(buf);
    uint32_t msg_type = 0;

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
    } else if (msg_type == 4) { // ConnectResponse
        esphome_api_ConnectResponse msg = esphome_api_ConnectResponse_init_zero;
        if (pb_decode(&stream, esphome_api_ConnectResponse_fields, &msg)) {
            printf("[STATE] ConnectResponse invalid_password=%d\n", msg.invalid_password);
            fflush(stdout);
        }
    } else if (msg_type == 21) { // BinarySensorStateResponse
        esphome_api_BinarySensorStateResponse msg = esphome_api_BinarySensorStateResponse_init_zero;
        if (pb_decode(&stream, esphome_api_BinarySensorStateResponse_fields, &msg)) {
            printf("[STATE] BinarySensor key=%u state=%s missing=%d\n", msg.key, msg.state ? "ON" : "OFF", msg.missing_state);
            fflush(stdout);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 21: %s\n", PB_GET_ERROR(&stream));
        }
    } else if (msg_type == 26) { // SwitchStateResponse
        esphome_api_SwitchStateResponse msg = esphome_api_SwitchStateResponse_init_zero;
        if (pb_decode(&stream, esphome_api_SwitchStateResponse_fields, &msg)) {
            printf("[STATE] Switch key=%u state=%s\n", msg.key, msg.state ? "ON" : "OFF");
            fflush(stdout);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 26: %s\n", PB_GET_ERROR(&stream));
        }
    } else if (msg_type == 25) { // SensorStateResponse
        esphome_api_SensorStateResponse msg = esphome_api_SensorStateResponse_init_zero;
        if (pb_decode(&stream, esphome_api_SensorStateResponse_fields, &msg)) {
            printf("[STATE] Sensor key=%u state=%.2f missing=%d\n", msg.key, msg.state, msg.missing_state);
            fflush(stdout);
        } else {
            fprintf(stderr, "[ERROR] Decode failed for type 25: %s\n", PB_GET_ERROR(&stream));
        }
    } else if (msg_type == 27) { // TextSensorStateResponse
        esphome_api_TextSensorStateResponse msg = esphome_api_TextSensorStateResponse_init_zero;
        if (pb_decode(&stream, esphome_api_TextSensorStateResponse_fields, &msg)) {
            printf("[STATE] TextSensor key=%u state=%s missing=%d\n", msg.key, msg.state, msg.missing_state);
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
