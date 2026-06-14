#include <stdio.h>
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/chachapoly.h>
#include <mbedtls/md.h>

#define NOISE_HASHLEN   32
#define NOISE_KEYLEN    32
struct esph_noise_ctx {
    uint8_t ck[NOISE_HASHLEN];      // chaining key
    uint8_t h[NOISE_HASHLEN];       // handshake hash
    uint8_t send_key[NOISE_KEYLEN];
    uint8_t recv_key[NOISE_KEYLEN];
    uint64_t send_nonce;
    uint64_t recv_nonce;

    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    mbedtls_ecdh_context ecdh;      // ephemeral
    mbedtls_chachapoly_context chachapoly;
};

int main() {
    printf("%zu\n", sizeof(struct esph_noise_ctx));
    return 0;
}
