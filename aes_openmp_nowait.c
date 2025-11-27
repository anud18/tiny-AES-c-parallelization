/*

This is an OpenMP implementation with NOWAIT clause.

The nowait clause removes the implicit barrier at the end of the
#pragma omp for directive, allowing threads that finish early to
proceed without waiting for slower threads.

This tests whether removing the barrier improves performance.

*/

#include <string.h>
#include <omp.h>
#include "aes.h"
#include "aes_openmp_nowait.h"

/*
 * Increment IV by a specified number of blocks
 */
static void IncrementIvBy(uint8_t* Iv, size_t blocks)
{
  size_t carry = blocks;
  for (int i = (AES_BLOCKLEN - 1); i >= 0 && carry > 0; --i)
  {
    size_t sum = Iv[i] + (carry & 0xFF);
    Iv[i] = (uint8_t)(sum & 0xFF);
    carry = (carry >> 8) + (sum >> 8);
  }
}

/*
 * OpenMP version with NOWAIT
 *
 * In this case, nowait may have minimal effect because:
 * 1. We use static scheduling (work evenly distributed)
 * 2. The parallel region ends immediately after the for loop
 * 3. There's an implicit barrier at the end of the parallel region
 *
 * However, it might help if:
 * - Thread workloads are slightly imbalanced due to system noise
 * - Compiler can optimize the code path differently
 */
void AES_CTR_xcrypt_buffer_openmp_nowait(struct AES_ctx* ctx, uint8_t* buf, size_t length)
{
  uint8_t buffer[AES_BLOCKLEN];

  size_t i;
  size_t num_blocks = length / AES_BLOCKLEN;

  // Save initial IV
  uint8_t initial_iv[AES_BLOCKLEN];
  memcpy(initial_iv, ctx->Iv, AES_BLOCKLEN);

  // Parallel block encryption with NOWAIT
  #pragma omp parallel private(buffer, i)
  {
    uint8_t thread_iv[AES_BLOCKLEN];
    struct AES_ctx thread_local_ctx;
    memcpy(thread_local_ctx.RoundKey, ctx->RoundKey, AES_keyExpSize);

    #pragma omp for schedule(static) nowait  // 👈 NOWAIT added
    for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx)
    {
      // Each thread computes the IV for its block index
      memcpy(thread_iv, initial_iv, AES_BLOCKLEN);
      IncrementIvBy(thread_iv, block_idx);

      // Encrypt the counter value
      memcpy(buffer, thread_iv, AES_BLOCKLEN);
      AES_ECB_encrypt(&thread_local_ctx, buffer);

      // XOR encrypted counter with plaintext/ciphertext
      size_t buf_idx = block_idx * AES_BLOCKLEN;
      for (i = 0; i < AES_BLOCKLEN; ++i)
      {
        buf[buf_idx + i] ^= buffer[i];
      }
    }
    // No barrier here due to nowait - threads can exit immediately
  }  // Implicit barrier at end of parallel region still exists

  // Update main context IV
  memcpy(ctx->Iv, initial_iv, AES_BLOCKLEN);
  IncrementIvBy(ctx->Iv, num_blocks);

  // Handle remaining bytes sequentially
  if (length % AES_BLOCKLEN)
  {
    struct AES_ctx remainder_ctx;
    memcpy(remainder_ctx.RoundKey, ctx->RoundKey, AES_keyExpSize);

    memcpy(buffer, ctx->Iv, AES_BLOCKLEN);
    AES_ECB_encrypt(&remainder_ctx, buffer);

    uint8_t bi = 0;
    for (i = (num_blocks * AES_BLOCKLEN); i < length; ++i, ++bi)
    {
      buf[i] = (buf[i] ^ buffer[bi]);
    }

    IncrementIvBy(ctx->Iv, 1);
  }
}
