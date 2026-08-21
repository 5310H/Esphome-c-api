#include "esphome_noise.h"
#include "esphome_transport.h"
#include <noise/protocol.h>
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int base64_decode(const char *input, unsigned char *output, size_t output_len) {
    static const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, j = 0, k = 0;
    unsigned char char_array_4[4], char_array_3[3];
    
    while (input[i] != '\0' && input[i] != '\n' && input[i] != '\r') {
        if (input[i] == ' ') { i++; continue; }
        
        char_array_4[j++] = input[i++];
        if (j == 4) {
            for (int m = 0; m < 4; m++) {
                if (char_array_4[m] == '=') char_array_4[m] = 0;
                else {
                    char *ptr = strchr(base64_chars, char_array_4[m]);
                    if (ptr) char_array_4[m] = ptr - base64_chars;
                    else char_array_4[m] = 0;
                }
            }
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (int m = 0; m < 3 && k < output_len; m++) {
                // If it's the 3rd byte but the 3rd char was padding, skip it.
                if (m == 1 && input[i - 2] == '=') break;
                if (m == 2 && input[i - 1] == '=') break;
                output[k++] = char_array_3[m];
            }
            j = 0;
        }
    }
    
    return k;
}

/* ESPHome's Noise handshake prologue is exactly strlen("NoiseAPIInit") bytes.
 * Appending NUL bytes changes the handshake hash and makes every encrypted
 * ESPHome device reject the client before it can receive ListEntities. */
#define ESPH_NOISE_PROLOGUE "NoiseAPIInit"
#define ESPH_NOISE_PROLOGUE_LEN 12

#define NOISE_LOGI(fmt, ...) printf("[NOISE] " fmt "\n", ##__VA_ARGS__)

struct esph_noise_ctx {
    NoiseHandshakeState *handshake;
    NoiseCipherState *send_cipher;
    NoiseCipherState *recv_cipher;
};

void esph_noise_free(esph_noise_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->handshake) noise_handshakestate_free(ctx->handshake);
    if (ctx->send_cipher) noise_cipherstate_free(ctx->send_cipher);
    if (ctx->recv_cipher) noise_cipherstate_free(ctx->recv_cipher);
    free(ctx);
}

// ---------------------------------------------------------------------------
// Noise Initialization
// ---------------------------------------------------------------------------
/**
 * Initializes the Noise-C cryptographic context for a new session.
 * Decodes the base64 pre-shared key (PSK) and instantiates the specific 
 * handshake pattern required by ESPHome: `Noise_NNpsk0_25519_ChaChaPoly_SHA256`.
 *
 * @param out_ctx Pointer to store the newly created context
 * @param psk_b64 Base64-encoded PSK from the user
 * @return 0 on success, -1 on error
 */
int esph_noise_init(esph_noise_ctx_t **out_ctx, const char *psk_b64) {
    if (noise_init() != NOISE_ERROR_NONE) {
        NOISE_LOGI("Failed to initialize noise-c library");
        return -1;
    }

    esph_noise_ctx_t *ctx = calloc(1, sizeof(esph_noise_ctx_t));
    if (!ctx) return -1;

    // Decode PSK
    uint8_t psk[32];
    int psk_len = base64_decode(psk_b64, psk, sizeof(psk));
    if (psk_len != 32) {
        NOISE_LOGI("PSK must be 32 bytes, got %d", psk_len);
        free(ctx);
        return -1;
    }

    int err = noise_handshakestate_new_by_name(&ctx->handshake, "NoisePSK_NN_25519_ChaChaPoly_SHA256", NOISE_ROLE_INITIATOR);
    if (err != NOISE_ERROR_NONE) {
        NOISE_LOGI("Failed to create handshake state: %d", err);
        free(ctx);
        return -1;
    }

    // [CRITICAL FIX]: ESPHome uses "Noise_NNpsk0_25519_ChaChaPoly_SHA256", 
    // but noise-c only supports "NoisePSK_NN_25519_ChaChaPoly_SHA256".
    // We must manually overwrite the initial Handshake Hash (h) and Chaining Key (ck)
    // with the SHA256 of the correct protocol name, otherwise the MAC will fail!
    uint8_t correct_hash[32] = {
        0xf0, 0xb8, 0x90, 0x82, 0x6f, 0xb8, 0xe6, 0x58, 
        0x4a, 0xf6, 0x94, 0x8d, 0x22, 0x71, 0x69, 0xc3, 
        0x81, 0xc9, 0xed, 0x6a, 0x4c, 0x1c, 0x90, 0xdf, 
        0x4b, 0x9f, 0xf4, 0x54, 0xfa, 0x94, 0x7f, 0x7b
    };
    struct NoiseHandshakeState_s *hs = (struct NoiseHandshakeState_s *)ctx->handshake;
    memcpy(hs->symmetric->h, correct_hash, 32);
    memcpy(hs->symmetric->ck, correct_hash, 32);

    err = noise_handshakestate_set_prologue(ctx->handshake, ESPH_NOISE_PROLOGUE,
                                             ESPH_NOISE_PROLOGUE_LEN);
    if (err != NOISE_ERROR_NONE) goto error;

    err = noise_handshakestate_set_pre_shared_key(ctx->handshake, psk, 32);
    if (err != NOISE_ERROR_NONE) goto error;

    err = noise_handshakestate_start(ctx->handshake);
    if (err != NOISE_ERROR_NONE) goto error;

    *out_ctx = ctx;
    return 0;

error:
    esph_noise_free(ctx);
    return -1;
}

// ---------------------------------------------------------------------------
// Noise Handshake Execution
// ---------------------------------------------------------------------------
/**
 * Drives the interactive Noise handshake over the TCP socket.
 * This function builds the initial encrypted `e` message, sends it,
 * and waits for the server to reply with its half of the handshake.
 * If successful, it splits the handshake state into persistent 
 * Send and Receive cipher states.
 *
 * @param ctx  The noise context
 * @param sock The active TCP socket
 * @return 0 on success, -1 on handshake failure
 */
int esph_noise_handshake(esph_noise_ctx_t *ctx, int sock) {
    uint8_t payload_buf[1024];
    NoiseBuffer mbuf;

    // 1. Send Client Hello
    uint8_t client_hello[3] = {0x01, 0x00, 0x00};
    if (esph_transport_send(sock, client_hello, 3) < 0) {
        NOISE_LOGI("Failed to send Client Hello");
        return -1;
    }
    NOISE_LOGI("Sent -> Client Hello (0x01 0x00 0x00)");

    // 2. Receive Server Hello
    uint8_t hello_header[3];
    if (esph_transport_recv(sock, hello_header, 3) < 0) {
        NOISE_LOGI("Failed to receive Server Hello header");
        return -1;
    }
    if (hello_header[0] != 0x01) {
        NOISE_LOGI("Invalid server hello indicator: %02x", hello_header[0]);
        return -1;
    }
    uint16_t hello_len = (hello_header[1] << 8) | hello_header[2];
    if (hello_len > 0) {
        uint8_t hello_body[1024];
        if (hello_len > sizeof(hello_body)) return -1;
        if (esph_transport_recv(sock, hello_body, hello_len) < 0) {
            NOISE_LOGI("Failed to receive Server Hello body");
            return -1;
        }
        NOISE_LOGI("Received Server Hello (len %d)", hello_len);
    }

    // 3. Send e message
    NOISE_LOGI("Writing -> e message");
    noise_buffer_set_output(mbuf, payload_buf, sizeof(payload_buf));
    int err = noise_handshakestate_write_message(ctx->handshake, &mbuf, NULL);
    if (err != NOISE_ERROR_NONE) {
        NOISE_LOGI("Failed to write handshake message e: %d", err);
        return -1;
    }

    uint16_t frame_len = mbuf.size + 1; // +1 for 0x00 indicator
    uint8_t out_frame[3 + 1 + 1024];
    out_frame[0] = 0x01;
    out_frame[1] = (frame_len >> 8) & 0xFF;
    out_frame[2] = frame_len & 0xFF;
    out_frame[3] = 0x00; // Indicator
    memcpy(out_frame + 4, mbuf.data, mbuf.size);

    if (esph_transport_send(sock, out_frame, 4 + mbuf.size) < 0) {
        NOISE_LOGI("Failed to send e message");
        return -1;
    }
    NOISE_LOGI("Sent -> e message (size %d)", (int)mbuf.size);

    // 4. Receive Handshake Response
    uint8_t header[3];
    if (esph_transport_recv(sock, header, 3) < 0) {
        return -1;
    }
    if (header[0] != 0x01) {
        NOISE_LOGI("Invalid server response header: %02x", header[0]);
        return -1;
    }
    uint16_t in_len = (header[1] << 8) | header[2];
    if (in_len == 0 || in_len > sizeof(payload_buf)) {
        NOISE_LOGI("Invalid server response length: %u", in_len);
        return -1;
    }

    uint8_t in_frame[1024];
    if (esph_transport_recv(sock, in_frame, in_len) < 0) {
        return -1;
    }

    uint8_t indicator = in_frame[0];
    if (indicator != 0x00) {
        NOISE_LOGI("Server reported error indicator: %02x", indicator);
        NOISE_LOGI("Error message length: %d", in_len - 1);
        printf("Error msg hex: ");
        for (int i = 1; i < in_len; i++) {
            printf("%02x ", in_frame[i]);
        }
        printf("\n");
        return -1;
    }

    uint8_t *msg_data = in_frame + 1;
    size_t msg_size = in_len - 1;

    NoiseBuffer payload;
    noise_buffer_set_input(mbuf, msg_data, msg_size);
    noise_buffer_set_output(payload, payload_buf, sizeof(payload_buf));

    err = noise_handshakestate_read_message(ctx->handshake, &mbuf, &payload);
    if (err != NOISE_ERROR_NONE) {
        NOISE_LOGI("Failed to read handshake message ee: %d", err);
        return -1;
    }
    NOISE_LOGI("Handshake successful!");

    err = noise_handshakestate_split(ctx->handshake, &ctx->send_cipher, &ctx->recv_cipher);
    if (err != NOISE_ERROR_NONE) {
        NOISE_LOGI("Failed to split handshake state: %d", err);
        return -1;
    }

    noise_handshakestate_free(ctx->handshake);
    ctx->handshake = NULL;

    return 0;
}

int esph_noise_encrypt(esph_noise_ctx_t *ctx,
                       const uint8_t *in, size_t in_len,
                       uint8_t *out, size_t *out_len) {
    if (!ctx->send_cipher) return -1;
    NoiseBuffer mbuf;
    noise_buffer_set_output(mbuf, out, *out_len);
    // write plaintext
    memcpy(mbuf.data, in, in_len);
    mbuf.size = in_len;
    int err = noise_cipherstate_encrypt(ctx->send_cipher, &mbuf);
    if (err != NOISE_ERROR_NONE) return -1;
    *out_len = mbuf.size;
    return 0;
}

int esph_noise_decrypt(esph_noise_ctx_t *ctx,
                       const uint8_t *in, size_t in_len,
                       uint8_t *out, size_t *out_len) {
    if (!ctx->recv_cipher) return -1;
    NoiseBuffer mbuf;
    // For decrypt, it decrypts in-place
    memcpy(out, in, in_len);
    noise_buffer_set_input(mbuf, out, in_len);
    int err = noise_cipherstate_decrypt(ctx->recv_cipher, &mbuf);
    if (err != NOISE_ERROR_NONE) return -1;
    *out_len = mbuf.size;
    return 0;
}
