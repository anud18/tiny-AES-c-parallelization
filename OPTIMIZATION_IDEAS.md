# 進階優化建議

目前的 OpenMP 實現已經避免了主要的 false sharing 問題，但還有幾個可以優化的地方：

## 1. ⚠️ Output Buffer False Sharing（最嚴重！）

### 問題
目前每個線程寫入連續的 16-byte blocks：
```c
// Thread 0: buf[0-15]
// Thread 1: buf[16-31]
// Thread 2: buf[32-47]
// Thread 3: buf[48-63]
// ^^^ 這 4 個 threads 寫入同一個 64-byte cache line！
```

這會造成 **output buffer 的 false sharing**！

### 檢測方法
```bash
perf c2c record ./benchmark.elf
perf c2c report --stdio | grep "buf"
```

應該會看到 `buf` 陣列有大量的 HITM (cache line 衝突)。

### 解決方案 1：Chunked Scheduling
```c
// 讓每個線程處理大的 chunk，減少 cache line 邊界重疊
#pragma omp for schedule(static, 64)  // 每個 chunk 處理 64 blocks = 1KB
for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx)
```

### 解決方案 2：每個線程先寫到本地 buffer
```c
#pragma omp parallel
{
    uint8_t local_buf[CHUNK_SIZE] __attribute__((aligned(64)));

    #pragma omp for schedule(static)
    for (size_t chunk = 0; chunk < num_chunks; ++chunk)
    {
        // 處理整個 chunk 到 local_buf
        // ...

        // 一次性複製回主 buffer
        memcpy(&buf[chunk * CHUNK_SIZE], local_buf, CHUNK_SIZE);
    }
}
```

---

## 2. 📐 Memory Alignment 優化

### 當前問題
Stack 變數可能沒有對齊到 cache line，導致：
- Cache line split（一個變數跨兩個 cache line）
- 額外的 memory access

### 解決方案
```c
#pragma omp parallel
{
    // 對齊到 64 bytes (cache line size)
    uint8_t thread_iv[AES_BLOCKLEN] __attribute__((aligned(64)));
    uint8_t buffer[AES_BLOCKLEN] __attribute__((aligned(64)));

    // 對齊 struct（176 bytes，跨 3 個 cache lines）
    struct AES_ctx thread_local_ctx __attribute__((aligned(64)));

    // ...
}
```

對 `struct AES_ctx` 也加上 alignment：
```c
struct AES_ctx
{
  uint8_t RoundKey[AES_keyExpSize];
  uint8_t Iv[AES_BLOCKLEN];
} __attribute__((aligned(64)));
```

---

## 3. 🔄 Loop Unrolling（XOR 迴圈）

### 當前代碼
```c
for (i = 0; i < AES_BLOCKLEN; ++i)
{
    buf[buf_idx + i] ^= buffer[i];
}
```

### 優化版本（手動展開）
```c
// AES_BLOCKLEN = 16, 可以展開成 4 個 4-byte XOR
uint32_t* buf32 = (uint32_t*)&buf[buf_idx];
uint32_t* buffer32 = (uint32_t*)buffer;

buf32[0] ^= buffer32[0];
buf32[1] ^= buffer32[1];
buf32[2] ^= buffer32[2];
buf32[3] ^= buffer32[3];
```

或用 compiler hint：
```c
#pragma GCC unroll 4
for (i = 0; i < AES_BLOCKLEN; i += 4)
{
    *(uint32_t*)&buf[buf_idx + i] ^= *(uint32_t*)&buffer[i];
}
```

---

## 4. 🚀 SIMD 優化（SSE/AVX）

### 使用 SSE2（128-bit = 16 bytes，剛好一個 AES block）
```c
#include <emmintrin.h>  // SSE2

// XOR 整個 block 用一個指令
__m128i* buf_vec = (__m128i*)&buf[buf_idx];
__m128i* buffer_vec = (__m128i*)buffer;
_mm_store_si128(buf_vec, _mm_xor_si128(_mm_load_si128(buf_vec),
                                        _mm_load_si128(buffer_vec)));
```

### 使用 AES-NI（硬體加速）
```c
#include <wmmintrin.h>  // AES-NI

// 用硬體指令做 AES 加密（比軟體實作快 3-5 倍）
__m128i AES_encrypt_block(__m128i plaintext, __m128i* round_keys)
{
    __m128i state = _mm_xor_si128(plaintext, round_keys[0]);

    for (int i = 1; i < 10; i++)
        state = _mm_aesenc_si128(state, round_keys[i]);

    return _mm_aesenclast_si128(state, round_keys[10]);
}
```

---

## 5. 🔮 Prefetching

```c
#pragma omp for schedule(static)
for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx)
{
    // Prefetch 下一個 block
    if (block_idx + 1 < num_blocks)
    {
        __builtin_prefetch(&buf[(block_idx + 1) * AES_BLOCKLEN], 1, 3);
    }

    // 處理當前 block
    // ...
}
```

---

## 6. 🎯 NUMA Awareness（多 socket 系統）

```c
// 綁定線程到特定 CPU
#pragma omp parallel
{
    int tid = omp_get_thread_num();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(tid, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    // ...
}
```

---

## 🎖️ 優化優先順序（從高到低）

1. **Output buffer false sharing**（最關鍵！可能影響 2-5x）
   - 用 chunked scheduling 或 local buffer

2. **SIMD/AES-NI**（3-5x 加速，如果 CPU 支援）
   - 需要重寫 AES 核心

3. **Memory alignment**（5-15% 提升）
   - 簡單，改動小

4. **Loop unrolling**（5-10% 提升）
   - 簡單，編譯器可能已經做了

5. **Prefetching**（0-10% 提升，取決於 workload）
   - 實驗性，可能無效或反效果

6. **NUMA**（只在多 socket 系統有用）

---

## 如何驗證

### 1. 檢測 output buffer false sharing
```bash
perf c2c record ./benchmark.elf
perf c2c report --stdio | grep -A 10 "Shared Data"
```

查看 HITM 數量和位置。

### 2. 查看 cache miss
```bash
perf stat -e cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses ./benchmark.elf
```

### 3. 查看 alignment
```bash
# 編譯時加上 -S 看組語
gcc -S -O3 -fopenmp aes_openmp.c

# 查看變數位置
grep "thread_iv" aes_openmp.s
```

---

## 建議的實作順序

1. **先用 perf c2c 確認是否有 output buffer false sharing**
2. **如果有，實作 chunked scheduling（最簡單）**
3. **加上 alignment attributes**
4. **如果需要更多性能，考慮 SIMD/AES-NI**

你想從哪一個開始實驗？我可以幫你實作！
