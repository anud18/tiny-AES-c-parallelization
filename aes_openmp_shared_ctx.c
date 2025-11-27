/*

This is an OpenMP implementation that uses a SHARED ctx (RoundKey).

All threads read from the same ctx->RoundKey instead of making a local copy.
This creates cache contention (all CPUs loading the same cache lines) but
NOT false sharing (because it's read-only).

This version demonstrates the performance impact of cache contention from
shared read-only data vs. thread-local copies.

*/

#include <string.h>
#include <omp.h>
#include "aes.h"
#include "aes_openmp_shared_ctx.h"

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
 * OpenMP version with SHARED ctx (read-only cache contention)
 *
 * Differences from optimized version:
 * - No thread_local_ctx copy
 * - All threads read from the same ctx->RoundKey
 * - This causes all CPUs to load the same cache lines
 * - Read-only access means no cache invalidation, but still contention
 */
void AES_CTR_xcrypt_buffer_openmp_shared_ctx(struct AES_ctx* ctx, uint8_t* buf, size_t length)
{
  uint8_t buffer[AES_BLOCKLEN];

  size_t i;
  size_t num_blocks = length / AES_BLOCKLEN;

  // Save initial IV
  uint8_t initial_iv[AES_BLOCKLEN];
  memcpy(initial_iv, ctx->Iv, AES_BLOCKLEN);

  // Parallel block encryption - SHARED ctx (read-only)
  #pragma omp parallel private(buffer, i)
  {
    uint8_t thread_iv[AES_BLOCKLEN];
    // NO local copy of ctx - all threads use the same ctx
    // This means all threads read from ctx->RoundKey (shared cache lines)

    #pragma omp for schedule(static)
    for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx)
    {
      // Each thread computes the IV for its block index
      memcpy(thread_iv, initial_iv, AES_BLOCKLEN);
      IncrementIvBy(thread_iv, block_idx);

      // Encrypt the counter value - using SHARED ctx
      memcpy(buffer, thread_iv, AES_BLOCKLEN);
      AES_ECB_encrypt(ctx, buffer);  // 👈 All threads read ctx->RoundKey

      // XOR encrypted counter with plaintext/ciphertext
      size_t buf_idx = block_idx * AES_BLOCKLEN;
      for (i = 0; i < AES_BLOCKLEN; ++i)
      {
        buf[buf_idx + i] ^= buffer[i];
      }
    }
  }  // End parallel region

  // Update main context IV
  memcpy(ctx->Iv, initial_iv, AES_BLOCKLEN);
  IncrementIvBy(ctx->Iv, num_blocks);

  // Handle remaining bytes sequentially
  if (length % AES_BLOCKLEN)
  {
    memcpy(buffer, ctx->Iv, AES_BLOCKLEN);
    AES_ECB_encrypt(ctx, buffer);

    uint8_t bi = 0;
    for (i = (num_blocks * AES_BLOCKLEN); i < length; ++i, ++bi)
    {
      buf[i] = (buf[i] ^ buffer[bi]);
    }

    IncrementIvBy(ctx->Iv, 1);
  }
}
