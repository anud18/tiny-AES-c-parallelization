/*

This is an OpenMP implementation that INTENTIONALLY creates false sharing
to demonstrate its performance impact.

False sharing occurs when multiple threads access different variables
that happen to reside on the same cache line (typically 64 bytes).
When one thread writes to its variable, the entire cache line is invalidated
in other CPU cores' caches, even though they're working on different data.

This version deliberately places thread-local buffers in a shared array
WITHOUT proper padding, causing them to share cache lines and degrade performance.

*/

#include <string.h>
#include <omp.h>
#include "aes.h"
#include "aes_openmp_false_sharing.h"

#define MAX_THREADS 256

// INTENTIONAL FALSE SHARING: Thread buffers packed tightly together
// Multiple thread buffers will share the same 64-byte cache line
// Thread 0,1,2,3 all share one cache line (4 * 16 bytes = 64 bytes)
static uint8_t thread_buffers[MAX_THREADS][AES_BLOCKLEN];  // NO PADDING!
static uint8_t thread_ivs[MAX_THREADS][AES_BLOCKLEN];       // NO PADDING!

/*
 * Increment IV by a specified number of blocks
 * Same as the optimized version
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
 * OpenMP version with INTENTIONAL FALSE SHARING
 *
 * This version uses shared arrays for thread buffers and IVs.
 * Because AES_BLOCKLEN is 16 bytes and cache lines are typically 64 bytes,
 * 4 threads will share each cache line.
 *
 * Every time a thread writes to its buffer, it invalidates the cache line
 * for the other 3 threads sharing that line, causing severe performance degradation.
 */
void AES_CTR_xcrypt_buffer_openmp_false_sharing(struct AES_ctx* ctx, uint8_t* buf, size_t length)
{
  size_t i;
  size_t num_blocks = length / AES_BLOCKLEN;

  // Save initial IV
  uint8_t initial_iv[AES_BLOCKLEN];
  memcpy(initial_iv, ctx->Iv, AES_BLOCKLEN);

  // Parallel block encryption with FALSE SHARING
  #pragma omp parallel private(i)
  {
    int thread_id = omp_get_thread_num();
    struct AES_ctx thread_local_ctx;
    memcpy(thread_local_ctx.RoundKey, ctx->RoundKey, AES_keyExpSize);

    #pragma omp for schedule(static)
    for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx)
    {
      // Use SHARED array indexed by thread ID - CAUSES FALSE SHARING!
      // Multiple threads write to adjacent memory locations in the same cache line
      uint8_t* my_iv = thread_ivs[thread_id];
      uint8_t* my_buffer = thread_buffers[thread_id];

      // Each thread computes the IV for its block index
      memcpy(my_iv, initial_iv, AES_BLOCKLEN);
      IncrementIvBy(my_iv, block_idx);

      // Encrypt the counter value
      memcpy(my_buffer, my_iv, AES_BLOCKLEN);
      AES_ECB_encrypt(&thread_local_ctx, my_buffer);

      // XOR encrypted counter with plaintext/ciphertext
      size_t buf_idx = block_idx * AES_BLOCKLEN;
      for (i = 0; i < AES_BLOCKLEN; ++i)
      {
        buf[buf_idx + i] ^= my_buffer[i];  // Reading from shared array - cache line invalidation!
      }
    }
  }  // End parallel region

  // Update main context IV
  memcpy(ctx->Iv, initial_iv, AES_BLOCKLEN);
  IncrementIvBy(ctx->Iv, num_blocks);

  // Handle remaining bytes sequentially
  if (length % AES_BLOCKLEN)
  {
    struct AES_ctx remainder_ctx;
    memcpy(remainder_ctx.RoundKey, ctx->RoundKey, AES_keyExpSize);

    uint8_t buffer[AES_BLOCKLEN];
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
