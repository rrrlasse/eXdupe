// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "tinyaes.h"
#include "internal/aes_impl.h"
#include "internal/ghash.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define HAS_RDTSC 1
#if defined(_MSC_VER)
#include <intrin.h>
static inline uint64_t rdtsc()
{
    return __rdtsc();
}
#else
static inline uint64_t rdtsc()
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
#endif
#else
#define HAS_RDTSC 0
#endif

static constexpr int ITERATIONS = 100;

struct BenchResult
{
    double mib_per_sec;
    double cycles_per_byte; // 0 if rdtsc not available
};

template<typename Fn> static BenchResult bench(const char *name, size_t data_size, Fn &&fn)
{
    // Warmup
    fn();

#if HAS_RDTSC
    uint64_t tsc_start = rdtsc();
#endif
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i)
    {
        fn();
    }
    auto end = std::chrono::high_resolution_clock::now();
#if HAS_RDTSC
    uint64_t tsc_end = rdtsc();
#endif

    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    double total_bytes = static_cast<double>(data_size) * ITERATIONS;
    double mib_per_sec = (total_bytes / (1024.0 * 1024.0)) / (ms / 1000.0);

    double cpb = 0.0;
#if HAS_RDTSC
    if (total_bytes > 0)
        cpb = static_cast<double>(tsc_end - tsc_start) / total_bytes;
#endif

    if (cpb > 0)
        std::printf("  %-40s %8.2f MiB/s  %6.2f c/B  (%6.2f ms)\n", name, mib_per_sec, cpb, ms);
    else
        std::printf("  %-40s %8.2f MiB/s  (%6.2f ms)\n", name, mib_per_sec, ms);

    return {mib_per_sec, cpb};
}

// Bench a single AES encrypt_block function at low level
template<typename EncFn>
static void bench_block_fn(const char *name, tinyaes::internal::key_expand_fn key_expand, EncFn enc_fn,
                           const uint8_t *key, size_t key_len)
{
    int rounds = tinyaes::internal::aes_rounds(key_len);
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS];
    key_expand(key, key_len, rk);

    uint8_t block[16] = {0};
    bench(
        name, 16,
        [&]()
        {
            for (int i = 0; i < 1000; ++i)
                enc_fn(rk, rounds, block, block);
        });
}

static void run_mode_benchmarks()
{
    static const size_t sizes[] = {16, 256, 1024, 4096, 16384, 65536, 262144, 1048576};
    static const size_t key_lens[] = {16, 24, 32};
    static const char *key_names[] = {"128", "192", "256"};

    for (size_t ki = 0; ki < 3; ++ki)
    {
        size_t kl = key_lens[ki];
        std::vector<uint8_t> key(kl, 0x42);
        std::vector<uint8_t> iv_16(16, 0x01);
        std::vector<uint8_t> iv_12(12, 0x01);
        std::vector<uint8_t> aad = {0xAA, 0xBB, 0xCC, 0xDD};

        std::printf("\n=== AES-%s ===\n", key_names[ki]);

        for (size_t sz : sizes)
        {
            // ECB needs block-aligned
            size_t ecb_sz = (sz / 16) * 16;
            if (ecb_sz == 0)
                ecb_sz = 16;
            std::vector<uint8_t> pt_ecb(ecb_sz, 0x55);
            std::vector<uint8_t> pt(sz, 0x55);

            char label[128];

            std::printf("\n--- %zu bytes ---\n", sz);

            std::snprintf(label, sizeof(label), "ECB-%s encrypt", key_names[ki]);
            bench(
                label, ecb_sz,
                [&]()
                {
                    std::vector<uint8_t> ct;
                    tinyaes::ecb_encrypt(key, pt_ecb, ct);
                });

            std::snprintf(label, sizeof(label), "CBC-%s encrypt", key_names[ki]);
            bench(
                label, ecb_sz,
                [&]()
                {
                    std::vector<uint8_t> ct;
                    tinyaes::cbc_encrypt(key, iv_16, pt_ecb, ct);
                });

            std::snprintf(label, sizeof(label), "CTR-%s encrypt", key_names[ki]);
            bench(
                label, sz,
                [&]()
                {
                    std::vector<uint8_t> ct;
                    tinyaes::ctr_crypt(key, iv_16, pt, ct);
                });

            std::snprintf(label, sizeof(label), "GCM-%s encrypt", key_names[ki]);
            bench(
                label, sz,
                [&]()
                {
                    std::vector<uint8_t> ct, tag;
                    tinyaes::gcm_encrypt(key, iv_12, aad, pt, ct, tag);
                });

            std::snprintf(label, sizeof(label), "GCM-%s decrypt", key_names[ki]);
            // Pre-encrypt for decrypt bench
            std::vector<uint8_t> ct_pre, tag_pre;
            tinyaes::gcm_encrypt(key, iv_12, aad, pt, ct_pre, tag_pre);
            bench(
                label, sz,
                [&]()
                {
                    std::vector<uint8_t> dec;
                    tinyaes::gcm_decrypt(key, iv_12, aad, ct_pre, tag_pre, dec);
                });
        }
    }
}

static void run_gcm_aad_benchmarks()
{
    std::printf("\n=== GCM AAD Variation (AES-128, 4096B plaintext) ===\n");

    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> iv(12, 0x01);
    std::vector<uint8_t> pt(4096, 0x55);

    static const size_t aad_sizes[] = {0, 16, 64, 256, 1024, 4096};
    for (size_t aad_sz : aad_sizes)
    {
        std::vector<uint8_t> aad(aad_sz, 0xAA);
        char label[128];
        std::snprintf(label, sizeof(label), "GCM-128 AAD=%zuB", aad_sz);
        bench(
            label, 4096 + aad_sz,
            [&]()
            {
                std::vector<uint8_t> ct, tag;
                tinyaes::gcm_encrypt(key, iv, aad, pt, ct, tag);
            });
    }
}

static void run_key_expansion_benchmarks()
{
    std::printf("\n=== Key Expansion ===\n");

    static const size_t key_lens[] = {16, 24, 32};
    static const char *key_names[] = {"128", "192", "256"};

    auto key_expand = tinyaes::internal::get_key_expand();

    for (size_t ki = 0; ki < 3; ++ki)
    {
        std::vector<uint8_t> key(key_lens[ki], 0x42);
        uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS];

        char label[128];
        std::snprintf(label, sizeof(label), "Key expand AES-%s (dispatched)", key_names[ki]);
        bench(
            label, key_lens[ki],
            [&]()
            {
                for (int i = 0; i < 1000; ++i)
                    key_expand(key.data(), key.size(), rk);
            });

        std::snprintf(label, sizeof(label), "Key expand AES-%s (portable)", key_names[ki]);
        bench(
            label, key_lens[ki],
            [&]()
            {
                for (int i = 0; i < 1000; ++i)
                    tinyaes::internal::aes_key_expand_portable(key.data(), key.size(), rk);
            });
    }
}

static void run_ghash_benchmarks()
{
    std::printf("\n=== GHASH ===\n");

    // Compute H from a fixed key
    uint8_t key_raw[16] = {0x42};
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key_raw, 16, rk);
    uint8_t H[16] = {0};
    uint8_t zero[16] = {0};
    tinyaes::internal::aes_encrypt_block_portable(rk, 10, zero, H);

    auto ghash_dispatch = tinyaes::internal::get_ghash();

    static const size_t sizes[] = {16, 256, 1024, 4096, 65536};
    for (size_t sz : sizes)
    {
        std::vector<uint8_t> data(sz, 0x55);

        char label[128];
        std::snprintf(label, sizeof(label), "GHASH %zuB (dispatched)", sz);
        bench(
            label, sz,
            [&]()
            {
                uint8_t Y[16] = {0};
                ghash_dispatch(H, data.data(), data.size(), Y);
            });

        std::snprintf(label, sizeof(label), "GHASH %zuB (portable)", sz);
        bench(
            label, sz,
            [&]()
            {
                uint8_t Y[16] = {0};
                tinyaes::internal::ghash_portable(H, data.data(), data.size(), Y);
            });
    }
}

static void run_backend_comparison()
{
    std::printf("\n=== Backend Comparison (single block encrypt x1000) ===\n");

    uint8_t key128[16] = {0x42};
    uint8_t key256[32] = {0x42};

    // Portable
    bench_block_fn("AES-128 portable", tinyaes::internal::aes_key_expand_portable,
                   tinyaes::internal::aes_encrypt_block_portable, key128, 16);
    bench_block_fn("AES-256 portable", tinyaes::internal::aes_key_expand_portable,
                   tinyaes::internal::aes_encrypt_block_portable, key256, 32);

    // Dispatched (may be AES-NI, ARM CE, or portable)
    bench_block_fn("AES-128 dispatched", tinyaes::internal::get_key_expand(), tinyaes::internal::get_encrypt_block(),
                   key128, 16);
    bench_block_fn("AES-256 dispatched", tinyaes::internal::get_key_expand(), tinyaes::internal::get_encrypt_block(),
                   key256, 32);
}

int main()
{
    std::printf("TinyAES Benchmarks (%d iterations per measurement)\n", ITERATIONS);
    std::printf("================================================================\n");

    run_backend_comparison();
    run_key_expansion_benchmarks();
    run_ghash_benchmarks();
    run_mode_benchmarks();
    run_gcm_aad_benchmarks();

    return 0;
}
