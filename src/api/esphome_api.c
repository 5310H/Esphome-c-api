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
int esph_frame_recv(esph_session_t *s, uint8_t *out, size_t *out_len);



// From proto_helpers.c
int esph_send_hello(esph_session_t *s);
int esph_send_connect(esph_session_t *s, const char *password);
int esph_send_subscribe_states(esph_session_t *s);
int esph_send_switch_command(esph_session_t *s, uint32_t key, int state);

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
        size_t len = 0;

        if (esph_frame_recv(s, buf, &len) != 0) {
            fprintf(stderr, "[API] HelloResponse recv failed\n");
            esph_disconnect(s);
            free(s);
            return NULL;
        }

        pb_istream_t stream = pb_istream_from_buffer(buf, len);
        uint64_t msg_type = 0;
        if (!pb_decode_varint(&stream, &msg_type)) {
            fprintf(stderr, "[API] Message type decode failed\n");
            esph_disconnect(s);
            free(s);
            return NULL;
        }

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
    if (esph_send_connect(s, psk) != 0) {
        fprintf(stderr, "[API] ConnectRequest failed\n");
        esph_disconnect(s);
        free(s);
        return NULL;
    }

    // 7. Receive ConnectResponse
    {
        uint8_t buf[512];
        size_t len = 0;

        if (esph_frame_recv(s, buf, &len) != 0) {
            fprintf(stderr, "[API] ConnectResponse recv failed\n");
            esph_disconnect(s);
            free(s);
            return NULL;
        }

        pb_istream_t stream = pb_istream_from_buffer(buf, len);
        uint64_t msg_type = 0;
        if (!pb_decode_varint(&stream, &msg_type)) {
            fprintf(stderr, "[API] Message type decode failed\n");
            esph_disconnect(s);
            free(s);
            return NULL;
        }

        if (msg_type != ESPH_MSG_CONNECT_RESPONSE) {
            fprintf(stderr, "[API] Unexpected frame type %u\n", (uint32_t)msg_type);
            esph_disconnect(s);
            free(s);
            return NULL;
        }

        esphome_api_ConnectResponse resp = esphome_api_ConnectResponse_init_zero;
        if (!pb_decode(&stream, esphome_api_ConnectResponse_fields, &resp)) {
            fprintf(stderr, "[API] ConnectResponse decode failed\n");
            esph_disconnect(s);
            free(s);
            return NULL;
        }

        if (resp.invalid_password) {
            fprintf(stderr, "[API] Invalid password\n");
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
// Subscribe to states
// ---------------------------------------------------------------------------
int esph_subscribe_states(esph_session_t *s)
{
    return esph_send_subscribe_states(s);
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
