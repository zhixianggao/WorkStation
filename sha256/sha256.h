#ifndef SHA256_H
#define SHA256_H
 
 
#include <stdint.h>
#include "sdk_errors.h"
 
#ifdef __cplusplus
extern "C" {
#endif
 
typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} sha256_context_t;
 
 
 
ret_code_t sha256_init(sha256_context_t *ctx);
 
ret_code_t sha256_update(sha256_context_t *ctx, const uint8_t * data, const size_t len);
 
ret_code_t sha256_final(sha256_context_t *ctx, uint8_t * hash, uint8_t le);
 
 
#ifdef __cplusplus
}
#endif

