#ifndef _AES_OPENMP_NOWAIT_H_
#define _AES_OPENMP_NOWAIT_H_

#include <stdint.h>
#include <stddef.h>
#include "aes.h"

/*
 * OpenMP parallel version with NOWAIT clause
 *
 * This version removes the implicit barrier after the parallel for loop.
 * Performance impact is likely minimal with static scheduling on balanced workloads.
 */
void AES_CTR_xcrypt_buffer_openmp_nowait(struct AES_ctx* ctx, uint8_t* buf, size_t length);

#endif // _AES_OPENMP_NOWAIT_H_
