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
    uint8_t hdr[2];
    hdr[0] = (enc_len >> 8) & 0xFF;
    hdr[1] = (enc_len >> 0) & 0xFF;

    // Send header
    if (esph_transport_send(s->sock, hdr, 2) != 2) {
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
int esph_frame_recv(esph_session_t *s,
                    uint8_t *out, size_t *out_len)
{
    uint8_t hdr[2];

    // Read 2‑byte length
    int r = esph_transport_recv(s->sock, hdr, 2);
    if (r != 2) {
        fprintf(stderr, "[FRAME] recv header failed\n");
        return -1;
    }

    size_t enc_len = ((size_t)hdr[0] << 8) | hdr[1];
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

    return 0;
}
