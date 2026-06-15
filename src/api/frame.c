#include "esphome_api.h"
#include "esphome_noise.h"
#include "esphome_transport.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Internal session struct (private to API layer)
// ---------------------------------------------------------------------------
struct esph_session {
    int sock;
    esph_noise_ctx_t *noise;
};

// ---------------------------------------------------------------------------
// Send encrypted frame
// ---------------------------------------------------------------------------
int esph_frame_send(esph_session_t *s,
                    const uint8_t *plaintext, size_t plen)
{
    uint8_t encbuf[2048];
    size_t enc_len = sizeof(encbuf);

    // Encrypt using Noise transport key
    if (esph_noise_encrypt(s->noise,
                           plaintext, plen,
                           encbuf, &enc_len) != 0) {
        fprintf(stderr, "[FRAME] encrypt failed\n");
        return -1;
    }

    // Prefix with 2‑byte big‑endian length
    uint8_t hdr[3];
    hdr[0] = 0x01;
    hdr[1] = (enc_len >> 8) & 0xFF;
    hdr[2] = (enc_len >> 0) & 0xFF;

    fprintf(stderr, "[FRAME] Sending encrypted frame (hdr: %02x %02x %02x, enc_len: %zu)\n", hdr[0], hdr[1], hdr[2], enc_len);

    // Send header
    if (esph_transport_send(s->sock, hdr, 3) != 3) {
        fprintf(stderr, "[FRAME] send header failed\n");
        return -1;
    }

    // Send encrypted payload
    if (esph_transport_send(s->sock, encbuf, enc_len) != (int)enc_len) {
        fprintf(stderr, "[FRAME] send payload failed\n");
        return -1;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Receive encrypted frame
// ---------------------------------------------------------------------------
int esph_frame_recv(esph_session_t *s, uint32_t *type,
                    uint8_t *out, size_t *out_len)
{
    uint8_t hdr[3];

    // Read 3-byte header (1 byte indicator, 2-byte length)
    int r = esph_transport_recv(s->sock, hdr, 3);
    if (r != 3) {
        fprintf(stderr, "[FRAME] recv header failed\n");
        return -1;
    }

    if (hdr[0] != 0x01) {
        fprintf(stderr, "[FRAME] invalid indicator %02x\n", hdr[0]);
    }

    size_t enc_len = ((size_t)hdr[1] << 8) | hdr[2];
    if (enc_len > 2048) {
        fprintf(stderr, "[FRAME] invalid frame length %zu\n", enc_len);
        return -1;
    }

    uint8_t encbuf[2048];

    // Read encrypted payload
    r = esph_transport_recv(s->sock, encbuf, enc_len);
    if (r != (int)enc_len) {
        fprintf(stderr, "[FRAME] recv payload failed\n");
        return -1;
    }

    // Decrypt using Noise transport key
    if (esph_noise_decrypt(s->noise,
                           encbuf, enc_len,
                           out, out_len) != 0) {
        fprintf(stderr, "[FRAME] decrypt failed\n");
        return -1;
    }

    if (*out_len < 4) {
        fprintf(stderr, "[FRAME] decrypted frame too small\n");
        return -1;
    }

    *type = ((uint32_t)out[0] << 8) | out[1];
    size_t payload_len = ((size_t)out[2] << 8) | out[3];

    if (payload_len + 4 != *out_len) {
        fprintf(stderr, "[FRAME] payload length mismatch\n");
        return -1;
    }

    // shift payload to the beginning of out buffer
    memmove(out, out + 4, payload_len);
    *out_len = payload_len;

    fprintf(stderr, "[FRAME] Received msg_type=%u, len=%zu, hex: ", *type, payload_len);
    for (size_t i = 0; i < payload_len; i++) {
        fprintf(stderr, "%02x ", out[i]);
    }
    fprintf(stderr, "\n");
    return 0;
}

int esph_frame_wait_readable(esph_session_t *s, int timeout_ms) {
    return esph_transport_wait_readable(s->sock, timeout_ms);
}

