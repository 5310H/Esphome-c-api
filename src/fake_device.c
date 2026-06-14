/* FULL FAKE ESPHOME DEVICE WITH ENCRYPTED ENTITIES */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#define read(s, b, l) recv(s, b, l, 0)
#define write(s, b, l) send(s, b, l, 0)
#define MSG_NOSIGNAL 0
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <stdbool.h>
#include <sodium.h>
#include <noise/protocol.h>

#include "api.pb.h"

#include "pb_encode.h"
#include "pb_decode.h"

enum {
    MSG_HELLO_REQUEST        = 1,
    MSG_HELLO_RESPONSE       = 2,
    MSG_ENCRYPTION_REQUEST   = 3,
    MSG_ENCRYPTION_RESPONSE  = 4,
    MSG_ENCRYPTED_MESSAGE    = 5,
    MSG_CONNECT_REQUEST      = 6,
    MSG_CONNECT_RESPONSE     = 7,
    MSG_SWITCH_STATE_RESPONSE = 17,
    MSG_PING_REQUEST         = 9,
    MSG_PING_RESPONSE        = 10,
    MSG_LIST_ENTITIES_REQUEST = 11,
    MSG_LIST_ENTITIES_SENSOR_RESPONSE = 12,
    MSG_LIST_ENTITIES_BINARY_SENSOR_RESPONSE = 13,
    MSG_LIST_ENTITIES_SWITCH_RESPONSE = 14,
    MSG_LIST_ENTITIES_LIGHT_RESPONSE = 15,
    MSG_LIST_ENTITIES_TEXT_SENSOR_RESPONSE = 16,
    MSG_LIST_ENTITIES_COVER_RESPONSE = 18,
    MSG_LIST_ENTITIES_DONE_RESPONSE = 19,
    MSG_SUBSCRIBE_STATES_REQUEST = 20,
    MSG_SENSOR_STATE_RESPONSE = 21,
    MSG_BINARY_SENSOR_STATE_RESPONSE = 22,
    MSG_TEXT_SENSOR_STATE_RESPONSE = 23,
    MSG_LIST_ENTITIES_NUMBER_RESPONSE = 24,
    MSG_NUMBER_STATE_RESPONSE = 25,
    MSG_SWITCH_COMMAND_REQUEST = 26,
    MSG_NUMBER_COMMAND_REQUEST = 27,
    MSG_LIST_ENTITIES_SELECT_RESPONSE = 28,
    MSG_SELECT_STATE_RESPONSE = 29,
    MSG_SELECT_COMMAND_REQUEST = 30,
    MSG_LIST_ENTITIES_LOCK_RESPONSE = 31,
    MSG_LOCK_STATE_RESPONSE = 32,
    MSG_LOCK_COMMAND_REQUEST = 33,
    MSG_COVER_STATE_RESPONSE = 34,
    MSG_COVER_COMMAND_REQUEST = 35,
    MSG_FAN_STATE_RESPONSE = 36,
    MSG_FAN_COMMAND_REQUEST = 37,
    MSG_LIST_ENTITIES_BUTTON_RESPONSE = 38,
    MSG_BUTTON_COMMAND_REQUEST = 39,
    MSG_LIST_ENTITIES_CLIMATE_RESPONSE = 40,
    MSG_CLIMATE_STATE_RESPONSE = 41,
    MSG_CLIMATE_COMMAND_REQUEST = 42,
    MSG_LIST_ENTITIES_MEDIA_PLAYER_RESPONSE = 43,
    MSG_MEDIA_PLAYER_STATE_RESPONSE = 44,
    MSG_MEDIA_PLAYER_COMMAND_REQUEST = 45,
    MSG_LIST_ENTITIES_FAN_RESPONSE = 49
};

/* ---------------------------------------------------------
   Nanopb byte callbacks
--------------------------------------------------------- */
typedef struct {
    const uint8_t *buffer;
    size_t length;
} pb_arg_t;

static bool encode_bytes_cb(pb_ostream_t *stream, const pb_field_iter_t *field, void * const *arg) {
    const pb_arg_t *data = (const pb_arg_t *)*arg;
    if (!data || !data->buffer) return true;
    if (!pb_encode_tag(stream, PB_WT_STRING, field->tag)) return false;
    return pb_encode_string(stream, data->buffer, data->length);
}

static bool decode_bytes_cb(pb_istream_t *stream, const pb_field_iter_t *field, void **arg) {
    pb_arg_t *dest = (pb_arg_t *)*arg;
    (void)field;

    if (stream->bytes_left > dest->length) return false;
    dest->length = stream->bytes_left;
    return pb_read(stream, (uint8_t *)dest->buffer, dest->length);
}

/* ---------------------------------------------------------
   4-byte type + 2-byte length framing
--------------------------------------------------------- */
static bool send_frame(int fd, uint32_t type, const uint8_t *data, size_t len) {
    uint8_t header[6];
    header[0] = (type >> 24) & 0xFF;
    header[1] = (type >> 16) & 0xFF;
    header[2] = (type >> 8) & 0xFF;
    header[3] = (type) & 0xFF;
    header[4] = (len >> 8) & 0xFF;
    header[5] = (len) & 0xFF;

    size_t total_sent = 0;
    while (total_sent < 6) {
        ssize_t n = send(fd, header + total_sent, 6 - total_sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        total_sent += n;
    }

    total_sent = 0;
    while (total_sent < len) {
        ssize_t n = send(fd, data + total_sent, len - total_sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        total_sent += n;
    }

    printf("FAKE DEVICE: Sent frame type=%u len=%zu\n", type, len);
    return true;
}

static bool recv_frame(int fd, uint32_t *type, uint8_t *payload, size_t max_len, size_t *len) {
    uint8_t header[6];
    size_t total_read = 0;
    while (total_read < 6) {
        ssize_t n = recv(fd, header + total_read, 6 - total_read, 0);
        if (n <= 0) return false;
        total_read += n;
    }

    *type = (header[0] << 24) | (header[1] << 16) | (header[2] << 8) | header[3];
    *len  = (header[4] << 8) | header[5];

    if (*len > max_len) return false;

    total_read = 0;
    while (total_read < *len) {
        ssize_t n = recv(fd, payload + total_read, *len - total_read, 0);
        if (n <= 0) return false;
        total_read += n;
    }

    printf("FAKE DEVICE: Received frame type=%u len=%zu\n", *type, *len);
    return true;
}

/* ---------------------------------------------------------
   Encrypt helper
--------------------------------------------------------- */
static NoiseCipherState *send_cipher = NULL;
static NoiseCipherState *recv_cipher = NULL;

static size_t encrypt_message(
    uint32_t inner_type,
    const uint8_t *plaintext,
    size_t plaintext_len,
    uint8_t *out)
{
    EncryptedMessage enc = EncryptedMessage_init_default;
    uint8_t ciphertext[2048];
    if (plaintext_len > 0 && plaintext != NULL) {
        memcpy(ciphertext, plaintext, plaintext_len);
    }
    NoiseBuffer mbuf;
    noise_buffer_set_inout(mbuf, ciphertext, plaintext_len, sizeof(ciphertext));

    if (noise_cipherstate_encrypt(send_cipher, &mbuf) != NOISE_ERROR_NONE) return 0;

    enc.type = inner_type;
    pb_arg_t arg_payload = {ciphertext, mbuf.size};
    enc.data.funcs.encode = encode_bytes_cb;
    enc.data.arg = &arg_payload;

    pb_ostream_t os = pb_ostream_from_buffer(out, 2048);
    if (!pb_encode(&os, EncryptedMessage_fields, &enc)) return 0;
    return os.bytes_written;
}

/* ---------------------------------------------------------
   Decrypt helper
--------------------------------------------------------- */
static bool decrypt_message(const uint8_t *payload,
                              size_t len,
                              uint8_t *out,
                              uint32_t *inner_type,
                              size_t *out_len)
{
    EncryptedMessage enc = EncryptedMessage_init_default;
    uint8_t ciphertext[2048];
    pb_arg_t arg = {ciphertext, sizeof(ciphertext)};
    
    enc.data.funcs.decode = decode_bytes_cb;
    enc.data.arg = &arg;

    pb_istream_t stream = pb_istream_from_buffer(payload, len);
    
    if (!pb_decode(&stream, EncryptedMessage_fields, &enc)) {
        printf("Failed to decode EncryptedMessage: %s\n", PB_GET_ERROR(&stream));
        return false;
    }
    
    if (inner_type) *inner_type = enc.type;

    NoiseBuffer mbuf;
    noise_buffer_set_output(mbuf, out, 2048);
    noise_buffer_set_input(mbuf, ciphertext, arg.length);

    if (noise_cipherstate_decrypt(recv_cipher, &mbuf) != NOISE_ERROR_NONE) {
        return false;
    }
    
    if (out_len) *out_len = mbuf.size;
    return true;
}

/* ---------------------------------------------------------
   MAIN
--------------------------------------------------------- */
int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (sodium_init() < 0) return 1;

#ifdef _WIN32
    {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            fprintf(stderr, "WSAStartup failed\n");
            return 1;
        }
    }
#endif

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(6053);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    // Device needs its own static key pair
    uint8_t device_priv[32], device_pub[32];
    crypto_box_keypair(device_pub, device_priv);
    char device_pub_b64[64];
    sodium_bin2base64(device_pub_b64, sizeof(device_pub_b64), device_pub, 32, sodium_base64_VARIANT_ORIGINAL);
    printf("FAKE DEVICE: Generated public key: %s\n", device_pub_b64);
    printf("FAKE DEVICE: Waiting on port 6053\n");

    socklen_t addrlen = sizeof(addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&addr, &addrlen);
    printf("FAKE DEVICE: Client connected\n");

    /* ---------------- NOISE HANDSHAKE (IK) ---------------- */
    NoiseHandshakeState *hs;
    int err;
    err = noise_handshakestate_new_by_name(&hs, "Noise_IK_25519_ChaChaPoly_SHA256", NOISE_ROLE_RESPONDER);
    if (err != NOISE_ERROR_NONE) {
        printf("FAKE DEVICE: noise_handshakestate_new_by_name failed: %d\n", err);
        return 1;
    }
    err = noise_handshakestate_set_prologue(hs, "NoiseAPIInit\0\0", 14);
    if (err != NOISE_ERROR_NONE) {
        printf("FAKE DEVICE: noise_handshakestate_set_prologue failed: %d\n", err);
        return 1;
    }

    err = noise_dhstate_set_keypair(noise_handshakestate_get_local_keypair_dh(hs), device_priv, 32, device_pub, 32);
    if (err != NOISE_ERROR_NONE) {
        printf("FAKE DEVICE: noise_dhstate_set_keypair failed: %d\n", err);
        return 1;
    }

    err = noise_handshakestate_start(hs);
    if (err != NOISE_ERROR_NONE) {
        printf("FAKE DEVICE: noise_handshakestate_start failed: %d\n", err);
        return 1;
    }

    // 1. Receive first handshake message
    uint8_t noise_hdr[3];
    printf("FAKE DEVICE: Waiting for client noise frame header...\n");
    if (read(client_fd, noise_hdr, 3) != 3) {
        printf("FAKE DEVICE: Failed to read client noise frame header\n");
        return 1;
    }
    uint16_t noise_len = (noise_hdr[1] << 8) | noise_hdr[2];
    printf("FAKE DEVICE: Reading client noise frame payload of size %u...\n", noise_len);
    uint8_t handshake_buf[512];
    if (read(client_fd, handshake_buf, noise_len) != noise_len) {
        printf("FAKE DEVICE: Failed to read client noise frame payload\n");
        return 1;
    }
    
    NoiseBuffer mbuf;
    noise_buffer_set_input(mbuf, handshake_buf, noise_len);
    err = noise_handshakestate_read_message(hs, &mbuf, NULL);
    if (err != NOISE_ERROR_NONE) {
        printf("FAKE DEVICE: noise_handshakestate_read_message failed: %d\n", err);
        return 1;
    }
    printf("FAKE DEVICE: Successfully read client handshake message!\n");

    // 2. Send second handshake message
    noise_buffer_set_output(mbuf, handshake_buf, sizeof(handshake_buf));
    err = noise_handshakestate_write_message(hs, &mbuf, NULL);
    if (err != NOISE_ERROR_NONE) {
        printf("FAKE DEVICE: noise_handshakestate_write_message failed: %d\n", err);
        return 1;
    }
    uint8_t noise_frame[3] = { 0x01, (mbuf.size >> 8) & 0xFF, mbuf.size & 0xFF };
    printf("FAKE DEVICE: Sending server noise frame header (%02x %02x %02x), payload size %zu\n", noise_frame[0], noise_frame[1], noise_frame[2], mbuf.size);
    if (write(client_fd, noise_frame, 3) != 3) {
        printf("FAKE DEVICE: Failed to write noise frame header\n");
        return 1;
    }
    if (write(client_fd, mbuf.data, mbuf.size) != (ssize_t)mbuf.size) {
        printf("FAKE DEVICE: Failed to write noise frame payload\n");
        return 1;
    }

    err = noise_handshakestate_split(hs, &send_cipher, &recv_cipher);
    if (err != NOISE_ERROR_NONE) {
        printf("FAKE DEVICE: noise_handshakestate_split failed: %d\n", err);
        return 1;
    }
    printf("FAKE DEVICE: Noise handshake split successfully!\n");

    /* ---------------------------------------------------------
       1. HelloRequest -> HelloResponse
    --------------------------------------------------------- */
    uint32_t type_in; size_t len_in; uint8_t recvbuf[2048]; uint8_t sendbuf[2048];
    recv_frame(client_fd, &type_in, recvbuf, sizeof(recvbuf), &len_in);
    HelloResponse hello = HelloResponse_init_default;
    hello.api_version_major = 1;
    hello.api_version_minor = 10; // Match client version
    pb_ostream_t os = pb_ostream_from_buffer(sendbuf, sizeof(sendbuf));
    pb_encode(&os, HelloResponse_fields, &hello);
    send_frame(client_fd, 2, sendbuf, os.bytes_written);

    /* ---------------------------------------------------------
       6. Receive encrypted ListEntitiesRequest
    --------------------------------------------------------- */
    uint8_t decrypted[512]; size_t dlen;
    recv_frame(client_fd, &type_in, recvbuf, sizeof(recvbuf), &len_in);
    if (!decrypt_message(recvbuf, len_in, decrypted, NULL, &dlen)) return 1;

    printf("FAKE DEVICE: Got ListEntitiesRequest\n");

    /* ---------------------------------------------------------
       7. Send fake entities
    --------------------------------------------------------- */
    const char *names[] = {
        "Fake Sensor",
        "Fake Binary Sensor",
        "Fake Switch",
        "Fake Light",
        "Fake Text Sensor",
        "Fake Number",
        "Fake Select",
        "Fake Button",
        "Fake Lock",
        "Fake Cover",
        "Fake Fan",
        "Fake Climate",
        "Fake Media Player"
    };

    uint32_t types[] = {
        MSG_LIST_ENTITIES_SENSOR_RESPONSE,
        MSG_LIST_ENTITIES_BINARY_SENSOR_RESPONSE,
        MSG_LIST_ENTITIES_SWITCH_RESPONSE,
        MSG_LIST_ENTITIES_LIGHT_RESPONSE,
        MSG_LIST_ENTITIES_TEXT_SENSOR_RESPONSE,
        MSG_LIST_ENTITIES_NUMBER_RESPONSE,
        MSG_LIST_ENTITIES_SELECT_RESPONSE,
        MSG_LIST_ENTITIES_BUTTON_RESPONSE,
        MSG_LIST_ENTITIES_LOCK_RESPONSE,
        MSG_LIST_ENTITIES_COVER_RESPONSE,
        MSG_LIST_ENTITIES_FAN_RESPONSE,
        MSG_LIST_ENTITIES_CLIMATE_RESPONSE,
        MSG_LIST_ENTITIES_MEDIA_PLAYER_RESPONSE
    };

    for (uint32_t i = 0; i < 13; i++) {
        uint8_t plain_ent[256];
        os = pb_ostream_from_buffer(plain_ent, sizeof(plain_ent));

        pb_arg_t name_arg = {(const uint8_t*)names[i], strlen(names[i])};

        switch (types[i]) {
            case MSG_LIST_ENTITIES_SENSOR_RESPONSE: {
                ListEntitiesSensorResponse s = ListEntitiesSensorResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesSensorResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_BINARY_SENSOR_RESPONSE: {
                ListEntitiesBinarySensorResponse s = ListEntitiesBinarySensorResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesBinarySensorResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_SWITCH_RESPONSE: {
                ListEntitiesSwitchResponse s = ListEntitiesSwitchResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesSwitchResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_LIGHT_RESPONSE: {
                ListEntitiesLightResponse s = ListEntitiesLightResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesLightResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_TEXT_SENSOR_RESPONSE: {
                ListEntitiesTextSensorResponse s = ListEntitiesTextSensorResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesTextSensorResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_NUMBER_RESPONSE: {
                ListEntitiesNumberResponse s = ListEntitiesNumberResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesNumberResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_SELECT_RESPONSE: {
                ListEntitiesSelectResponse s = ListEntitiesSelectResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesSelectResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_LOCK_RESPONSE: {
                ListEntitiesLockResponse s = ListEntitiesLockResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesLockResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_BUTTON_RESPONSE: {
                ListEntitiesButtonResponse s = ListEntitiesButtonResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesButtonResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_COVER_RESPONSE: {
                ListEntitiesCoverResponse s = ListEntitiesCoverResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesCoverResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_FAN_RESPONSE: {
                ListEntitiesFanResponse s = ListEntitiesFanResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesFanResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_CLIMATE_RESPONSE: {
                ListEntitiesClimateResponse s = ListEntitiesClimateResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesClimateResponse_fields, &s);
                break;
            }
            case MSG_LIST_ENTITIES_MEDIA_PLAYER_RESPONSE: {
                ListEntitiesMediaPlayerResponse s = ListEntitiesMediaPlayerResponse_init_default;
                s.key = i + 1;
                s.name.funcs.encode = encode_bytes_cb;
                s.name.arg = &name_arg;
                pb_encode(&os, ListEntitiesMediaPlayerResponse_fields, &s);
                break;
            }
        }

        size_t ent_len = encrypt_message(types[i], plain_ent, os.bytes_written, sendbuf);
        send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, ent_len);
    }

    /* ---------------------------------------------------------
       8. DONE
    --------------------------------------------------------- */
    ListEntitiesDoneResponse done = ListEntitiesDoneResponse_init_default;

    uint8_t plain_done[64];
    os = pb_ostream_from_buffer(plain_done, sizeof(plain_done));
    pb_encode(&os, ListEntitiesDoneResponse_fields, &done);

    size_t done_len = encrypt_message(MSG_LIST_ENTITIES_DONE_RESPONSE, plain_done, os.bytes_written, sendbuf);

    send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, done_len);

    printf("FAKE DEVICE: Sent all entities + DONE\n");

    /* ---------------------------------------------------------
       9. Event Loop (Simulator)
    --------------------------------------------------------- */
    uint8_t inner_plain[512];
    uint32_t inner_type_in;
    size_t inner_plen;

    printf("FAKE DEVICE: Session active. Handling requests...\n");
    while (recv_frame(client_fd, &type_in, recvbuf, sizeof(recvbuf), &len_in)) {
        if (!decrypt_message(recvbuf, len_in, inner_plain, &inner_type_in, &inner_plen)) {
            continue;
        }
        
        printf("FAKE DEVICE: Received decrypted inner type %u\n", inner_type_in);
        
        if (inner_type_in == MSG_SUBSCRIBE_STATES_REQUEST) {
            printf("FAKE DEVICE: Client subscribed to states. Sending mock update...\n");
            SensorStateResponse state = SensorStateResponse_init_default;
            state.key = 1;
            state.state = 23.5f;
            uint8_t state_plain[64];
            pb_ostream_t os_sub = pb_ostream_from_buffer(state_plain, sizeof(state_plain));
            pb_encode(&os_sub, SensorStateResponse_fields, &state);
            size_t state_enc_len = encrypt_message(MSG_SENSOR_STATE_RESPONSE, state_plain, os_sub.bytes_written, sendbuf);
            send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, state_enc_len);

            // Mock Text Sensor Initial State
            TextSensorStateResponse ts_state = TextSensorStateResponse_init_default;
            ts_state.key = 5; // Matches "Fake Text Sensor" index
            pb_arg_t ts_arg = {(const uint8_t*)"Online", 6};
            ts_state.state.funcs.encode = encode_bytes_cb;
            ts_state.state.arg = &ts_arg;
            uint8_t ts_plain[128];
            pb_ostream_t os_ts = pb_ostream_from_buffer(ts_plain, sizeof(ts_plain));
            pb_encode(&os_ts, TextSensorStateResponse_fields, &ts_state);
            size_t ts_enc_len = encrypt_message(MSG_TEXT_SENSOR_STATE_RESPONSE, ts_plain, os_ts.bytes_written, sendbuf);
            send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, ts_enc_len);
        } else if (inner_type_in == MSG_PING_REQUEST) {
            printf("FAKE DEVICE: Received Ping. Sending Pong...\n");
            PingResponse pres = PingResponse_init_default;
            uint8_t plain_pong[64];
            os = pb_ostream_from_buffer(plain_pong, sizeof(plain_pong));
            pb_encode(&os, PingResponse_fields, &pres);
            size_t pong_enc_len = encrypt_message(MSG_PING_RESPONSE, plain_pong, os.bytes_written, sendbuf);
            send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, pong_enc_len);
        } else if (inner_type_in == MSG_SWITCH_COMMAND_REQUEST) {
            SwitchCommandRequest sw_in = SwitchCommandRequest_init_default;
            pb_istream_t is_cmd = pb_istream_from_buffer(inner_plain, inner_plen);
            if (pb_decode(&is_cmd, SwitchCommandRequest_fields, &sw_in)) {
                printf("FAKE DEVICE: Received SwitchCommand for Key %u -> %s\n", 
                       sw_in.key, sw_in.state ? "ON" : "OFF");
                
                // Respond with state update confirmation
                SwitchStateResponse sw_out = SwitchStateResponse_init_default;
                sw_out.key = sw_in.key;
                sw_out.state = sw_in.state;
                uint8_t sw_plain[64];
                os = pb_ostream_from_buffer(sw_plain, sizeof(sw_plain));
                pb_encode(&os, SwitchStateResponse_fields, &sw_out);
                size_t sw_enc_len = encrypt_message(MSG_SWITCH_STATE_RESPONSE, sw_plain, os.bytes_written, sendbuf);
                send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, sw_enc_len);
            }
        } else if (inner_type_in == MSG_NUMBER_COMMAND_REQUEST) {
            NumberCommandRequest num_in = NumberCommandRequest_init_default;
            pb_istream_t is_cmd = pb_istream_from_buffer(inner_plain, inner_plen);
            if (pb_decode(&is_cmd, NumberCommandRequest_fields, &num_in)) {
                printf("FAKE DEVICE: NumberCommand Key %u -> %f\n", num_in.key, num_in.state);
                NumberStateResponse num_out = NumberStateResponse_init_default;
                num_out.key = num_in.key;
                num_out.state = num_in.state;
                uint8_t p[64]; pb_ostream_t os_cmd = pb_ostream_from_buffer(p, sizeof(p));
                pb_encode(&os_cmd, NumberStateResponse_fields, &num_out);
                size_t e = encrypt_message(MSG_NUMBER_STATE_RESPONSE, p, os_cmd.bytes_written, sendbuf);
                send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, e);
            }
        } else if (inner_type_in == MSG_SELECT_COMMAND_REQUEST) {
            SelectCommandRequest sel_in = SelectCommandRequest_init_default;
            char sel_buf[64]; pb_arg_t sel_arg = {(uint8_t*)sel_buf, sizeof(sel_buf)-1};
            sel_in.state.funcs.decode = decode_bytes_cb;
            sel_in.state.arg = &sel_arg;
            pb_istream_t is_cmd = pb_istream_from_buffer(inner_plain, inner_plen);
            if (pb_decode(&is_cmd, SelectCommandRequest_fields, &sel_in)) {
                sel_buf[sel_arg.length] = '\0';
                printf("FAKE DEVICE: SelectCommand Key %u -> %s\n", sel_in.key, sel_buf);
                SelectStateResponse sel_out = SelectStateResponse_init_default;
                sel_out.key = sel_in.key;
                pb_arg_t out_arg = {(uint8_t*)sel_buf, strlen(sel_buf)};
                sel_out.state.funcs.encode = encode_bytes_cb;
                sel_out.state.arg = &out_arg;
                uint8_t p[128]; pb_ostream_t os_cmd = pb_ostream_from_buffer(p, sizeof(p)); // Corrected: os_cmd was not defined
                pb_encode(&os_cmd, SelectStateResponse_fields, &sel_out);
                size_t e = encrypt_message(MSG_SELECT_STATE_RESPONSE, p, os_cmd.bytes_written, sendbuf);
                send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, e);
            }
        } else if (inner_type_in == MSG_BUTTON_COMMAND_REQUEST) {
            ButtonCommandRequest btn_in = ButtonCommandRequest_init_default;
            pb_istream_t is_cmd = pb_istream_from_buffer(inner_plain, inner_plen);
            if (pb_decode(&is_cmd, ButtonCommandRequest_fields, &btn_in)) {
                printf("FAKE DEVICE: Button %u pressed!\n", btn_in.key);
            }
        } else if (inner_type_in == MSG_LOCK_COMMAND_REQUEST) {
            LockCommandRequest lock_in = LockCommandRequest_init_default;
            pb_istream_t is_cmd = pb_istream_from_buffer(inner_plain, inner_plen);
            if (pb_decode(&is_cmd, LockCommandRequest_fields, &lock_in)) {
                printf("FAKE DEVICE: LockCommand Key %u -> %d\n", lock_in.key, lock_in.command);
                LockStateResponse lock_out = LockStateResponse_init_default;
                lock_out.key = lock_in.key;
                lock_out.state = (lock_in.command == 0) ? 1 : 0; // Toggle logic
                uint8_t p[64]; pb_ostream_t os_cmd = pb_ostream_from_buffer(p, sizeof(p));
                pb_encode(&os_cmd, LockStateResponse_fields, &lock_out);
                size_t e = encrypt_message(MSG_LOCK_STATE_RESPONSE, p, os_cmd.bytes_written, sendbuf);
                send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, e);
            }
        } else if (inner_type_in == MSG_COVER_COMMAND_REQUEST) {
            CoverCommandRequest cov_in = CoverCommandRequest_init_default;
            pb_istream_t is_cmd = pb_istream_from_buffer(inner_plain, inner_plen);
            if (pb_decode(&is_cmd, CoverCommandRequest_fields, &cov_in)) {
                printf("FAKE DEVICE: CoverCommand Key %u -> cmd:%d\n", cov_in.key, cov_in.has_position);
                CoverStateResponse cov_out = CoverStateResponse_init_default;
                cov_out.key = cov_in.key;
                cov_out.position = cov_in.has_position ? cov_in.position : 1.0f;
                uint8_t p[64]; pb_ostream_t os_cmd = pb_ostream_from_buffer(p, sizeof(p));
                pb_encode(&os_cmd, CoverStateResponse_fields, &cov_out);
                size_t e = encrypt_message(MSG_COVER_STATE_RESPONSE, p, os_cmd.bytes_written, sendbuf);
                send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, e);
            }
        } else if (inner_type_in == MSG_FAN_COMMAND_REQUEST) {
            FanCommandRequest fan_in = FanCommandRequest_init_default;
            pb_istream_t is_cmd = pb_istream_from_buffer(inner_plain, inner_plen);
            if (pb_decode(&is_cmd, FanCommandRequest_fields, &fan_in)) {
                printf("FAKE DEVICE: FanCommand Key %u -> %s\n", fan_in.key, fan_in.state ? "ON" : "OFF");
                FanStateResponse fan_out = FanStateResponse_init_default;
                fan_out.key = fan_in.key;
                fan_out.state = fan_in.state;
                uint8_t p[64]; pb_ostream_t os_cmd = pb_ostream_from_buffer(p, sizeof(p));
                pb_encode(&os_cmd, FanStateResponse_fields, &fan_out);
                size_t e = encrypt_message(MSG_FAN_STATE_RESPONSE, p, os_cmd.bytes_written, sendbuf);
                send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, e);
            }
        } else if (inner_type_in == MSG_CLIMATE_COMMAND_REQUEST) {
            ClimateCommandRequest cli_in = ClimateCommandRequest_init_default;
            pb_istream_t is_cmd = pb_istream_from_buffer(inner_plain, inner_plen);
            if (pb_decode(&is_cmd, ClimateCommandRequest_fields, &cli_in)) {
                printf("FAKE DEVICE: ClimateCommand Key %u -> target:%f\n", cli_in.key, cli_in.target_temperature);
                ClimateStateResponse cli_out = ClimateStateResponse_init_default;
                cli_out.key = cli_in.key;
                cli_out.mode = cli_in.has_mode ? cli_in.mode : 1;
                cli_out.target_temperature = cli_in.has_target_temperature ? cli_in.target_temperature : 21.0f;
                cli_out.current_temperature = 22.5f;
                uint8_t p[128]; pb_ostream_t os_cmd = pb_ostream_from_buffer(p, sizeof(p));
                pb_encode(&os_cmd, ClimateStateResponse_fields, &cli_out);
                size_t e = encrypt_message(MSG_CLIMATE_STATE_RESPONSE, p, os_cmd.bytes_written, sendbuf);
                send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, e);
            }
        } else if (inner_type_in == MSG_MEDIA_PLAYER_COMMAND_REQUEST) {
            MediaPlayerCommandRequest med_in = MediaPlayerCommandRequest_init_default;
            pb_istream_t is_cmd = pb_istream_from_buffer(inner_plain, inner_plen);
            if (pb_decode(&is_cmd, MediaPlayerCommandRequest_fields, &med_in)) {
                printf("FAKE DEVICE: MediaPlayerCommand Key %u -> cmd:%d\n", med_in.key, med_in.command);
                MediaPlayerStateResponse med_out = MediaPlayerStateResponse_init_default;
                med_out.key = med_in.key;
                med_out.state = 1; // Playing
                med_out.volume = 0.5f;
                uint8_t p[128]; pb_ostream_t os_cmd = pb_ostream_from_buffer(p, sizeof(p));
                pb_encode(&os_cmd, MediaPlayerStateResponse_fields, &med_out);
                size_t e = encrypt_message(MSG_MEDIA_PLAYER_STATE_RESPONSE, p, os_cmd.bytes_written, sendbuf);
                send_frame(client_fd, MSG_ENCRYPTED_MESSAGE, sendbuf, e);
            }
        }
    }

    printf("FAKE DEVICE: Connection closed.\n");

    close(client_fd);
    close(server_fd);
    return 0;
}