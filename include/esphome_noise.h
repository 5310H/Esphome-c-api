#ifndef ESPHOME_NOISE_H
#define ESPHOME_NOISE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque Noise context.
// The struct is defined privately inside noise.c.
typedef struct esph_noise_ctx esph_noise_ctx_t;

/**
 * Initialize a Noise_NNpsk0_25519_ChaChaPoly_SHA256 context.
 *
 * @param ctx  Pointer to an allocated esph_noise_ctx_t
 * @param psk  Null-terminated PSK string (ESPHome uses 32-byte PSK base64 encoded)
 * @return 0 on success, <0 on failure
 */
int esph_noise_init(esph_noise_ctx_t **ctx, const char *psk_b64);

/**
 * Perform the client-side Noise NNpsk0 handshake.
 *
 * @param ctx  Initialized Noise context
 * @param sock Connected TCP socket
 * @return 0 on success, <0 on failure
 */
int esph_noise_handshake(esph_noise_ctx_t *ctx, int sock);

/**
 * Encrypt a plaintext message using the derived send_key.
 *
 * @param ctx      Noise context
 * @param in       Plaintext buffer
 * @param in_len   Length of plaintext
 * @param out      Output buffer (ciphertext + tag)
 * @param out_len  Output length
 * @return 0 on success, <0 on failure
 */
int esph_noise_encrypt(esph_noise_ctx_t *ctx,
                       const uint8_t *in, size_t in_len,
                       uint8_t *out, size_t *out_len);

/**
 * Decrypt a ciphertext message using the derived recv_key.
 *
 * @param ctx      Noise context
 * @param in       Ciphertext + tag buffer
 * @param in_len   Length of ciphertext+tag
 * @param out      Output plaintext buffer
 * @param out_len  Output plaintext length
 * @return 0 on success, <0 on failure
 */
int esph_noise_decrypt(esph_noise_ctx_t *ctx,
                       const uint8_t *in, size_t in_len,
                       uint8_t *out, size_t *out_len);

/**
 * Free the noise context.
 */
void esph_noise_free(esph_noise_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
