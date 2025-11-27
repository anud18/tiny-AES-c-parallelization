# False Sharing Profiling and Benchmarking

這個專案包含了兩個 OpenMP 實現版本：
1. **優化版本** (`aes_openmp.c`) - 避免了 false sharing
2. **False Sharing 版本** (`aes_openmp_false_sharing.c`) - 故意製造 false sharing 來展示其性能影響

## 什麼是 False Sharing？

False sharing 發生在多個線程訪問同一個 cache line（通常 64 bytes）上的不同變數時。即使這些線程操作的是不同的變數，但由於它們在同一個 cache line，當一個線程寫入時，會導致其他 CPU core 的 cache line 失效，造成嚴重的性能下降。

## 編譯和運行 Benchmark

```bash
# 編譯
make benchmark.elf

# 運行 benchmark（會自動測試兩個版本）
./benchmark.elf
```

Benchmark 會測試：
- 正確性檢查（確保兩個版本結果相同）
- 優化版本的性能（1, 2, 4, 8, 16 線程）
- False Sharing 版本的性能（1, 2, 4, 8, 16 線程）
- 性能對比和加速比

結果會輸出到：
- 終端（格式化的輸出）
- `benchmark_results.csv`（可用於繪圖分析）

## 使用 perf 工具檢測 False Sharing

### 1. 基本 perf 統計

查看基本性能計數器：

```bash
# 優化版本
perf stat -e cache-misses,cache-references,L1-dcache-load-misses,L1-dcache-loads ./benchmark.elf

# 觀察 cache miss rate 和 cache reference
```

### 2. 使用 perf c2c 檢測 False Sharing

`perf c2c` (cache-to-cache) 是專門用來檢測 false sharing 的工具：

```bash
# 記錄 cache line 共享的詳細資料
perf c2c record -F 60000 -a --all-user ./benchmark.elf

# 分析報告
perf c2c report

# 或產生 HTML 報告（需要安裝 perf 的相關套件）
perf c2c report --stdio > false_sharing_report.txt
```

在報告中重點關注：
- **Shared Data Cache Line Table**：顯示哪些 cache line 被多個 CPU 共享
- **HITM** (Hit Modified)：顯示 cache line 衝突的次數
- **Store Reference** 和 **Load Reference**：顯示訪問模式

### 3. 詳細的 cache 事件分析

```bash
# 記錄所有 cache 相關事件
perf record -e cache-misses,cache-references \
            -e L1-dcache-load-misses,L1-dcache-loads \
            -e LLC-load-misses,LLC-loads \
            ./benchmark.elf

# 查看報告
perf report

# 查看詳細註解（顯示哪些代碼行導致 cache miss）
perf annotate
```

### 4. 使用 perf 的 memory events

在支援的處理器上（Intel）：

```bash
# 記錄 memory access patterns
perf mem record ./benchmark.elf

# 分析 memory access
perf mem report
```

## 使用 Valgrind/Cachegrind 分析

Cachegrind 可以模擬 CPU cache 並報告 cache miss：

```bash
# 運行 cachegrind
valgrind --tool=cachegrind --cache-sim=yes ./benchmark.elf

# 會產生 cachegrind.out.<pid> 文件

# 查看報告
cg_annotate cachegrind.out.<pid>
```

關注指標：
- **D1 miss rate**：L1 data cache miss rate
- **LL miss rate**：Last level cache miss rate

## 使用 Intel VTune Profiler（如果可用）

如果在 Intel 平台上，VTune 提供最詳細的分析：

```bash
# Memory Access Analysis
vtune -collect memory-access -result-dir vtune_results ./benchmark.elf

# 查看報告
vtune-gui vtune_results
```

在 VTune 中重點查看：
- **False Sharing** 標籤頁
- **Memory Access** 熱點
- **CPU utilization** 和 **thread contention**

## 使用 AMD uProf（AMD 平台）

AMD 平台上的性能分析工具：

```bash
# Cache analysis
AMDuProfCLI collect --config tbp ./benchmark.elf

# 查看報告
AMDuProfCLI report
```

## 預期結果

在多線程運行時，你應該會看到：

### 優化版本（無 false sharing）：
- 隨著線程數增加，吞吐量接近線性增長
- Cache miss rate 較低
- CPU utilization 高

### False Sharing 版本：
- 多線程時性能下降或增長緩慢
- **非常高的 cache miss rate**
- **HITM 事件數量高**（在 perf c2c 中）
- CPU 花費大量時間等待 cache coherency
- 可能看到 4 個線程一組的模式（因為 16 bytes × 4 = 64 bytes cache line）

## 關鍵性能指標對比

| 指標 | 優化版本 | False Sharing 版本 |
|------|---------|-------------------|
| Cache Miss Rate | 低 (~5-10%) | 高 (~30-60%) |
| HITM 事件 | 少 | 多 |
| 加速比（4 threads） | ~3.5x | ~1.2-1.5x |
| 加速比（8 threads） | ~6-7x | ~1.5-2x |

## 程式碼差異說明

### 優化版本（aes_openmp.c）
```c
// 每個線程有自己的私有變數（在 stack 上）
#pragma omp parallel private(buffer, i)
{
    uint8_t thread_iv[AES_BLOCKLEN];      // 每個線程獨立的副本
    uint8_t buffer[AES_BLOCKLEN];          // 每個線程獨立的副本
    struct AES_ctx thread_local_ctx;       // 每個線程獨立的副本
    // ...
}
```

### False Sharing 版本（aes_openmp_false_sharing.c）
```c
// 全域共享陣列，沒有 padding
static uint8_t thread_buffers[MAX_THREADS][AES_BLOCKLEN];  // 緊密排列！
static uint8_t thread_ivs[MAX_THREADS][AES_BLOCKLEN];      // 緊密排列！

#pragma omp parallel private(i)
{
    int thread_id = omp_get_thread_num();
    uint8_t* my_buffer = thread_buffers[thread_id];  // 指向共享記憶體
    uint8_t* my_iv = thread_ivs[thread_id];          // 指向共享記憶體
    // thread 0, 1, 2, 3 的 buffer 在同一個 64-byte cache line！
}
```

## 如何避免 False Sharing

1. **使用私有變數**（stack 上）而非共享陣列
2. **添加 padding**：在陣列元素間添加填充到 cache line 大小
3. **使用 `__attribute__((aligned(64)))`** 確保變數對齊到 cache line
4. **使用 thread-local storage** (`__thread` 或 `thread_local`)

範例：
```c
// 添加 padding 避免 false sharing
struct padded_data {
    uint8_t data[16];
    uint8_t padding[64 - 16];  // 填充到 64 bytes
} __attribute__((aligned(64)));

static struct padded_data thread_buffers[MAX_THREADS];
```

## 延伸閱讀

- [Intel: Avoiding and Identifying False Sharing](https://software.intel.com/content/www/us/en/develop/articles/avoiding-and-identifying-false-sharing-among-threads.html)
- [perf c2c documentation](https://man7.org/linux/man-pages/man1/perf-c2c.1.html)
- [Mechanical Sympathy: False Sharing](https://mechanical-sympathy.blogspot.com/2011/07/false-sharing.html)
- [Linux kernel: Documentation/perf-c2c.txt](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/perf/Documentation/perf-c2c.txt)
