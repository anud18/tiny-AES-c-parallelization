#ifndef _AES_OPENMP_SHARED_CTX_H_
#define _AES_OPENMP_SHARED_CTX_H_

#include <stdint.h>
#include <stddef.h>
#include "aes.h"

/*
 * OpenMP parallel version that uses SHARED ctx (RoundKey)
 *
 * This version demonstrates the performance impact of cache contention
 * from multiple threads reading the same memory (ctx->RoundKey).
 *
 * This is NOT false sharing (read-only access), but causes cache contention.
 */
void AES_CTR_xcrypt_buffer_openmp_shared_ctx(struct AES_ctx* ctx, uint8_t* buf, size_t length);

#endif // _AES_OPENMP_SHARED_CTX_H_
