#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rnet_sha256_ctx {
    uint32_t h[8];
    uint64_t total;
    uint8_t buffer[64];
    size_t buffered;
} rnet_sha256_ctx;

/* Self-contained public-domain SHA-256 implementation. */
void rnet_sha256_init(rnet_sha256_ctx* ctx);
void rnet_sha256_update(rnet_sha256_ctx* ctx, const uint8_t* data, size_t len);
void rnet_sha256_final(rnet_sha256_ctx* ctx, uint8_t out[32]);
void rnet_sha256_compute(const uint8_t *data, size_t len, uint8_t out[32]);

#ifdef __cplusplus
}
#endif
