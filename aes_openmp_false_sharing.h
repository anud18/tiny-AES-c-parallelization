#ifndef _AES_OPENMP_FALSE_SHARING_H_
#define _AES_OPENMP_FALSE_SHARING_H_

#include <stdint.h>
#include <stddef.h>
#include "aes.h"

/*
 * OpenMP parallel version of AES CTR mode with INTENTIONAL FALSE SHARING
 *
 * WARNING: This implementation deliberately creates false sharing to demonstrate
 * its performance impact. DO NOT use this in production!
 *
 * For the optimized version without false sharing, use aes_openmp.h
 */
void AES_CTR_xcrypt_buffer_openmp_false_sharing(struct AES_ctx* ctx, uint8_t* buf, size_t length);

#endif // _AES_OPENMP_FALSE_SHARING_H_
