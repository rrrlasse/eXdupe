// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include <assert.h>
#include <immintrin.h>
#include <smmintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>

#include "blake3/c/blake3.h"
#include "xxHash/xxh3.h"
#include "xxHash/xxhash.h"
#include "../gsl/gsl"

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd/lib/zstd.h"

#include "libexdupe.h"

struct zstd_params_s {
    ZSTD_CCtx* cctx;
    ZSTD_DCtx* dctx;
    ZSTD_CDict* cdict;
    ZSTD_parameters zparams;
    ZSTD_customMem cmem;
};

// fixme, global
state_compress_t state_c;
zstd_params_s* state_d;

//#define EXDUPE_THREADTEST

#ifdef EXDUPE_THREADTEST
static void threadtest_delay(void) {
    // Without tsan we want to avoid the synchronizing instructions that std::atomic and 
    // local static initializers emits which would defeat the purpose of threadtest_delay.
#if defined(__SANITIZE_THREAD__)
    static std::atomic<uint64_t> state;
#else
    static uint64_t state;
#endif
    const uint64_t multiplier = 1664525;
    const uint64_t increment = 1013904223;
    state = (multiplier * state + increment);

    // increase constant to run faster
    int64_t f = state % 100'000;

    if (f > 1'000) {
        return;
    }
    else if (f > 100) {
        sched_yield();
    }
    else if (f > 9) {
        Sleep(state % 100);
    }
    else {
        Sleep(state % 1000);
    }
}
#else
static void threadtest_delay(void) {}
#endif

[[maybe_unused]] static int pthread_mutex_lock_wrapper(pthread_mutex_t *m) {
    threadtest_delay();
    int r = pthread_mutex_lock(m);
    threadtest_delay();
    return r;
}

[[maybe_unused]] static int pthread_mutex_unlock_wrapper(pthread_mutex_t *m) {
    threadtest_delay();
    int r = pthread_mutex_unlock(m);
    threadtest_delay();
    return r;
}

[[maybe_unused]] static int pthread_cond_wait_wrapper(pthread_cond_t *c, pthread_mutex_t *m) {
    threadtest_delay();
    int r = pthread_cond_wait(c, m);
    threadtest_delay();
    return r;
}

[[maybe_unused]] static int pthread_cond_broadcast_wrapper(pthread_cond_t *c) {
    threadtest_delay();
    int r = pthread_cond_broadcast(c);
    threadtest_delay();
    return r;
}

[[maybe_unused]] static int pthread_cond_signal_wrapper(pthread_cond_t *c) {
    threadtest_delay();
    int r = pthread_cond_signal(c);
    threadtest_delay();
    return r;
}
[[maybe_unused]] static int pthread_mutex_trylock_wrapper(pthread_mutex_t *mutex) {
    threadtest_delay();
    int r = pthread_mutex_trylock(mutex);
    threadtest_delay();
    return r;
}

[[maybe_unused]] static void sched_yield_wrapper() {
    threadtest_delay();
    sched_yield();
    threadtest_delay();
}

static void ll2str(uint64_t l, char* dst, int bytes) {
    unsigned char* dst2 = (unsigned char*)dst;
    while (bytes > 0) {
        *dst2 = (l & 0xff);
        dst2++;
        l = l >> 8;
        bytes--;
    }
}

static uint64_t str2ll(const void* src, int bytes) {
    unsigned char* src2 = (unsigned char*)src;
    uint64_t l = 0;
    while (bytes > 0) {
        bytes--;
        l = l << 8;
        l = (l | *(src2 + bytes));
    }
    return l;
}

static size_t write_hashblock(hashblock_t* h, char* dst) {
    char* orig_dst = dst;
    size_t t;

    if (h->hash[0] == 0) {
        return 0;
    }

    for (t = 0; t < SLOTS; t++) {

        ll2str(h->hash[t], dst, 4);
        if (h->hash[t] == 0) {
            dst += 4;
            break;
        }

        ll2str(h->entry[t].offset, dst + 4, 8);
        ll2str(h->entry[t].slide, dst + 12, 2);
        ll2str(h->entry[t].first_byte, dst + 14, 1);

        memcpy(dst + 15, h->entry[t].sha, HASH_SIZE);

        dst += 15 + HASH_SIZE;
    }
    return dst - orig_dst;
}

static size_t read_hashblock(hashblock_t* h, char* src) {
    char* orig_src = src;
    bool nulls = false;
    for (size_t t = 0; t < SLOTS; t++) {

        if (!nulls) {
            h->hash[t] = gsl::narrow<uint32_t>(str2ll(src, 4));
            src += 4;
        }

        if (h->hash[t] == 0) {
            nulls = true;
        }

        if (nulls) {
            h->hash[t] = 0;
            h->entry[t].offset = 0;
            h->entry[t].slide = 0;
            h->entry[t].first_byte = 0;
            memset(h->entry[t].sha, 0, HASH_SIZE);
        }
        else {
            h->entry[t].offset = str2ll(src, 8);
            h->entry[t].slide = gsl::narrow<uint16_t>(str2ll(src + 8, 2));
            h->entry[t].first_byte = gsl::narrow<uint8_t>(str2ll(src + 10, 1));
            memcpy(h->entry[t].sha, src + 11, HASH_SIZE);
            src += 11 + HASH_SIZE;
        }
    }
    return src - orig_src;
}

static void print_hashblock(hashblock_t* h) {
    for (size_t t = 0; t < SLOTS; t++) {
        std::wcerr << L"(" << std::hex 
            << h->hash[t] << std::dec
            << L"," << h->entry[t].offset
            << L"," << h->entry[t].slide
            << L"," << (int)h->entry[t].sha[0]
            << L") \n";
    }
}

static bool dd_equal(const void *src1, const void *src2, size_t len) {
    char *s1 = (char *)src1;
    char *s2 = (char *)src2;
    for (size_t i = 0; i < len; i++) {
        if (s1[i] != s2[i]) {
            return false;
        }
    }
    return true;
}


static hash_t* lookup(uint32_t hash, bool large) {
    hashblock_t* table = (large ? state_c.large_table : state_c.small_table);
    uint32_t row = hash % ((large ? state_c.large_entries : state_c.small_entries));
    hashblock_t& e = table[row];

    for(uint64_t i = 0; i < SLOTS; i++) {
        if (e.hash[i] == 0) {
            return 0;
        }
        else if(e.hash[i] == hash && hash != 0) {
            return &e.entry[i];
        }
    }
    return 0;
}


static bool add(hash_t value, uint32_t hash, bool large) {
    hashblock_t* table = (large ? state_c.large_table : state_c.small_table);
    uint32_t row = hash % ((large ? state_c.large_entries : state_c.small_entries));
    hashblock_t& e = table[row];

    for(uint64_t i = 0; i < SLOTS; i++) {
        if(e.hash[i] == hash) {
            return dd_equal(e.entry[i].sha, value.sha, HASH_SIZE);
        }
        if(e.hash[i] == 0 && hash != 0) {
            e.hash[i] = hash;
            e.entry[i] = value;
            return true;
        }
    }
    return false;
}

void print_fillratio() {
    // todo
}

static uint64_t minimum(uint64_t a, uint64_t b) {
    return a > b ? b : a;
}


static char *zstd_init() {
    zstd_params_s *zstd_params = (zstd_params_s *)malloc(sizeof(zstd_params_s));
    if (!zstd_params) {
        return NULL;
    }
    zstd_params->cctx = ZSTD_createCCtx();
    zstd_params->dctx = ZSTD_createDCtx();
    zstd_params->cdict = NULL;
    return (char *)zstd_params;
}


static int64_t zstd_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, int level, char *workmem) {
    zstd_params_s *zstd_params = (zstd_params_s *)workmem;
    size_t ret = ZSTD_compressCCtx(zstd_params->cctx, outbuf, outsize, inbuf, insize, level);
    return ret;
}


static bool is_compressible(char* inbuf, size_t insize, char* outbuf, char* workmem) {
    zstd_params_s* zstd_params = (zstd_params_s*)workmem;
    size_t fast = 2048;
    size_t slow = 4096;
    if (insize > 128 * 1024) {
        if (
            ZSTD_compressCCtx(zstd_params->cctx, outbuf, insize, inbuf + insize / 2, fast, 1) >= fast &&
            ZSTD_compressCCtx(zstd_params->cctx, outbuf, insize, inbuf + insize / 3 * 0, slow, 1) >= slow &&
            ZSTD_compressCCtx(zstd_params->cctx, outbuf, insize, inbuf + insize / 3 * 1, slow, 1) >= slow &&
            ZSTD_compressCCtx(zstd_params->cctx, outbuf, insize, inbuf + insize / 3 * 2, slow, 1) >= slow &&
            ZSTD_compressCCtx(zstd_params->cctx, outbuf, insize, inbuf + insize - slow, slow, 1) >= slow &&
            true) {
            return false;
        }
    }
    return true;
}

static int64_t zstd_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, zstd_params_s* zstd_params) {
    return ZSTD_decompressDCtx(zstd_params->dctx, outbuf, outsize, inbuf, insize);
}

static void hash(const void *src, size_t len, uint64_t salt, char *dst) {
    if (state_c.g_crypto_hash) {
        char s[sizeof(salt)];
        ll2str(salt, (char*)s, sizeof(salt));
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, s, sizeof(s));
        blake3_hasher_update(&hasher, src, len);
        uint8_t output[BLAKE3_OUT_LEN];
        blake3_hasher_finalize(&hasher, output, BLAKE3_OUT_LEN);
        memcpy(dst, output, HASH_SIZE);
    } else {
        XXH64_hash_t s{ salt };
        XXH128_hash_t hash = XXH128(src, len, s);
        memcpy(dst, &hash, HASH_SIZE);
    }
}

static uint64_t hash64(const void* src, size_t len) {
    char h[HASH_SIZE];
    hash(src, len, 0, h);
    return str2ll(h, 8);
}

void print_table() {
#if 1
    std::wcerr << L"\nsmall:\n";
    for (uint64_t i = 0; i < state_c.small_entries; i++) {
        print_hashblock(&state_c.small_table[i]);
    }
    std::wcerr << L"\nlarge:\n";
    for (uint64_t i = 0; i < state_c.large_entries; i++) {
        print_hashblock(&state_c.large_table[i]);
    }
    std::wcerr << "\n\n";
#endif
}

static bool in_use(size_t i) {
    for(size_t t = 0; t < SLOTS; t++) {
        if(state_c.small_table[i].hash[t] != 0) {
            return true;
        }
    }
    return false;
}

static size_t equ(size_t start) {
    bool used = in_use(start);
    size_t count = 1;
    size_t total_entries = state_c.small_entries + state_c.large_entries;
    while(start + count < total_entries) {
        if(used != in_use(start + count)) {
            break;
        }
        count++;
    }
    return count;
}

static uint32_t quick(const char* src, size_t len) {
    uint64_t res = 0;
    res += *(uint64_t*)&src[0];
    res += *(uint64_t*)&src[len / 3 * 1 - 1];
    res += *(uint64_t*)&src[len / 3 * 2 - 2];
    res += *(uint64_t*)&src[len - 8 - 3];
    res = res + (res >> 32);
    return static_cast<uint32_t>(res);
}

static uint32_t window(const char* src, size_t len, const char** pos) {
    size_t i = 0;
    size_t slide = minimum(len / 2, 65536); // slide must be able to fit in hash_t.O. Todo, type safe
    size_t block = len - slide;
    int16_t b = len >= state_c.LARGE_BLOCK ? 32767 - 32 : 32767 - 256;
    size_t none = static_cast<size_t>(-1);
    size_t match = none;

    for (i = 0; i + 32 < slide; i += 32) {
        __m256i src1 = _mm256_loadu_si256((__m256i*)(&src[i]));
        __m256i src2 = _mm256_loadu_si256((__m256i*)(&src[i + block - 32 - 1]));
        __m256i sum1 = _mm256_add_epi16(src1, src2);

        __m256i src3 = _mm256_loadu_si256((__m256i*)(&src[i + 1]));
        __m256i src4 = _mm256_loadu_si256((__m256i*)(&src[i + block - 32]));
        __m256i sum2 = _mm256_add_epi16(src3, src4);

        sum1 = _mm256_mullo_epi16(sum1, sum1);
        sum2 = _mm256_mullo_epi16(sum2, sum2);

        __m256i comparison1 = _mm256_cmpgt_epi16(sum1, _mm256_set1_epi16(b));
        __m256i comparison2 = _mm256_cmpgt_epi16(sum2, _mm256_set1_epi16(b));
        __m256i comparison = _mm256_or_si256(comparison1, comparison2);

        auto larger = _mm256_movemask_epi8(comparison);

        if (larger != 0) {
            auto b1 = _mm256_movemask_epi8(comparison1); 
            auto b2 = _mm256_movemask_epi8(comparison2);
#if defined _MSC_VER
            unsigned int off1 = _tzcnt_u32(static_cast<unsigned int>(b1));
            unsigned int off2 = 1 + _tzcnt_u32(static_cast<unsigned int>(b2));
#else
            unsigned int off1 = __builtin_ctz(static_cast<unsigned int>(b1));
            unsigned int off2 = 1 + __builtin_ctz(static_cast<unsigned int>(b2));
#endif
            unsigned int off = (int)minimum(off1, off2);
            match = i + off;
            break;
        }
    }

    if (match == none) {
        for (; i < slide; i += 1) {
            int16_t src1 = *(int16_t*)&src[i];
            int16_t src2 = *(int16_t*)&src[i + block - 32 - 1];
            int16_t sum = src1 + src2;
            sum = sum * sum;
            if (sum > b) {
                match = i;
                break;
            }
        }
    }

    // todo simplify below cases
    if (match == none) {
        match = slide;
    }

    if (pos != 0) {
        *pos = src + match;
    }

    if (match == slide) {
        return 0;
    }
    else {
        return 1 + quick(src + match, len - slide - 8);
    }
}

const static char *dub(const char *src, uint64_t pay, size_t len, size_t block, uint64_t *payload_ref) {
    assert(block == state_c.LARGE_BLOCK || block == state_c.SMALL_BLOCK);
    const char *w_pos;
    const char *orig_src = src;
    const char *last_src = src + len - 1;
    uint32_t w = window(src, block, &w_pos);
    const char* collision = 0;

    while (src + block <= last_src) {
        hash_t* e;
        // CAUTION: Outside mutex, assume reading garbage and that the garbage can change between reads
        if (w != 0 && (e = lookup(w, block == state_c.LARGE_BLOCK))) {
            hash_t e_cpy;
            pthread_mutex_lock_wrapper(&state_c.table_mutex);
            e = lookup(w, block == state_c.LARGE_BLOCK);
            if(e) {
                e_cpy = *e;
            }
            pthread_mutex_unlock_wrapper(&state_c.table_mutex);
            if (e && w_pos - e_cpy.slide > src && w_pos - e_cpy.slide <= last_src) {
                src = w_pos - e_cpy.slide;
            }

            if (src + block <= last_src && e && e_cpy.first_byte == (uint8_t)src[0] && e_cpy.offset + block < pay + (src - orig_src)) {
                char s[HASH_SIZE];

                if (block == state_c.LARGE_BLOCK) {
                    char tmp[8 * 1024]; // todo
                    assert(sizeof(tmp) >= state_c.LARGE_BLOCK / state_c.SMALL_BLOCK * HASH_SIZE);
                    uint32_t k;
                    for (k = 0; k < state_c.LARGE_BLOCK / state_c.SMALL_BLOCK; k++) {
                        hash(src + k * state_c.SMALL_BLOCK, state_c.SMALL_BLOCK, state_c.g_hash_salt, tmp + k * HASH_SIZE);
                    }
                    hash(tmp, state_c.LARGE_BLOCK / state_c.SMALL_BLOCK * HASH_SIZE, state_c.g_hash_salt, s);
                } else {
                    hash(src, block, state_c.g_hash_salt, s);
                }

                if (e_cpy.offset + block < pay + (src - orig_src) && dd_equal(s, e_cpy.sha, HASH_SIZE)) {
                    collision = 0; // prevent skipping data now because more matches may be near by
                    *payload_ref = e_cpy.offset;
                    return src;
                } else {
                    src += collision > src - 1024 ? 1024 : 32;
                    char c = *src;
                    while (src <= last_src && *src == c) {
                        src++;
                    }
                    collision = src;
                }
            } else {
                src = w_pos;
            }
        } else {
            src = w_pos;
        }

        src++;

        if (w_pos < src) {
            if(src + block >= last_src) {
                return 0;
            }

            w = window(src, block, &w_pos);
        }
    }

    return 0;
}

static bool hashat(const char *src, uint64_t pay, size_t len, bool large, char *hash) {
    const char *o;
    uint32_t w = window(src, len, &o);
    if(w != 0) {
        pthread_mutex_lock_wrapper(&state_c.table_mutex);
        hash_t e;            
        e.first_byte = (uint8_t)src[0];
        e.offset = pay;
        memcpy(e.sha, hash, HASH_SIZE);
        e.slide = static_cast<uint16_t>(o - src);
        bool added = add(e, w, large);
        if(!added) {
            if(large) {
                state_c.congested_large += len;
            }
            else {
                state_c.congested_small += len;
            }
        }
        pthread_mutex_unlock_wrapper(&state_c.table_mutex);
        return true;
    }
    else {
        return false;
    }
}

static size_t write_match(size_t length, uint64_t payload, char *dst) {
   // wcerr << L"match = " << payload << L"," << length << L"\n";
    if (length > 0) {
        if(length == state_c.LARGE_BLOCK) {
            state_c.largehits += length;
        }
        else {
            state_c.smallhits += length;
        }
        dst[0] = DUP_REFERENCE;
        ll2str(DUP_HEADER_LEN, dst + 1, 4);
        ll2str(length, dst + 5, 4);
        ll2str(payload, dst + 9, 8); 
        return DUP_HEADER_LEN;
    }
    return 0;
}

static size_t write_literals(const char *src, size_t length, char *dst, int thread_id, bool entropy) {
  //  wcerr << L"literal = " << length << L" ";
    assert(state_c.level >= 0 && state_c.level <= 3);

    if (length == 0) {
        return 0;
    }
    size_t packet_size = 0;
    bool compressible = true;
        
    if(!entropy) {
        compressible = is_compressible((char*)src, length, dst, state_c.jobs[thread_id].zstd);
    }
    else {
        state_c.high_entropy += length;
    }

    if (state_c.level == 0 || entropy || !compressible) {
        dst[DUP_HEADER_LEN] = '0';
        memcpy(dst + DUP_HEADER_LEN + 1, src, length);
        // DUP_HEADER, '0', raw data
        packet_size = 1 + length;
    } else if (state_c.level >= 1 && state_c.level <= 3) {
        int zstd_level = state_c.level == 1 ? 1 : state_c.level == 2 ? 10 : 19;
        dst[DUP_HEADER_LEN] = char(state_c.level + '0');
        int64_t r = zstd_compress((char *)src, length, dst + DUP_HEADER_LEN + 1, 2 * length + 1000000, zstd_level, state_c.jobs[thread_id].zstd);
        packet_size = 1 + r;
    }

    packet_size += DUP_HEADER_LEN;

    dst[0] = DUP_LITERAL;
    ll2str(packet_size, dst + 1, 4);
    ll2str(length, dst + 5, 4);
    ll2str(0, dst + 9, 8);

    state_c.stored_as_literals += length;
    state_c.literals_compressed_size += packet_size;

    return packet_size;
}

static void hash_chunk(const char *src, uint64_t pay, size_t length) {
    char tmp[512 * HASH_SIZE];
    assert(sizeof(tmp) >= HASH_SIZE * state_c.LARGE_BLOCK / state_c.SMALL_BLOCK);

    size_t small_blocks = length / state_c.SMALL_BLOCK;
    uint32_t smalls = 0;
    uint32_t j = 0;

    for (j = 0; j < small_blocks; j++) {
        hash(src + j * state_c.SMALL_BLOCK, state_c.SMALL_BLOCK, state_c.g_hash_salt, tmp + smalls * HASH_SIZE);
        bool success_small = hashat(src + j * state_c.SMALL_BLOCK, pay + j * state_c.SMALL_BLOCK, state_c.SMALL_BLOCK, false, tmp + smalls * HASH_SIZE);
        if(!success_small) {
            state_c.anomalies_small += state_c.SMALL_BLOCK;
        }

        smalls++;
        if (smalls == state_c.LARGE_BLOCK / state_c.SMALL_BLOCK) {
            char tmp2[HASH_SIZE];
            hash(tmp, smalls * HASH_SIZE, state_c.g_hash_salt, tmp2);
            bool success_large = hashat(src + (j + 1) * state_c.SMALL_BLOCK - state_c.LARGE_BLOCK, pay + (j + 1) * state_c.SMALL_BLOCK - state_c.LARGE_BLOCK, state_c.LARGE_BLOCK, true, tmp2);
            if(!success_large) {
                state_c.anomalies_large += state_c.SMALL_BLOCK;
            }

            smalls = 0;
        }
    }
    return;
}


static size_t process_chunk(const char* src, uint64_t pay, size_t length, char* dst, int thread_id) {
    // Fixme, make this function recursive, it will be *alot* smaller and simpler and will also coalesce
    // LARGE_BLOCK blocks with ease. This is way too messy...
    const char* upto;
    const char* src_orig = src;
    char* dst_orig = dst;
    const char* last = src + length - 1;

    while (src <= last) {
        uint64_t ref = 0;
        const char* match = 0;

        if (src + state_c.LARGE_BLOCK - 1 <= last) {
            match = dub(src, pay + (src - src_orig), last - src, state_c.LARGE_BLOCK, &ref);
        }
        upto = (match == 0 ? last : match - 1);

        while (src <= upto) {
            uint64_t ref_s = 0;
            const char* match_s = 0;
            size_t n = 0;

            if (src + state_c.SMALL_BLOCK - 1 <= upto) {
                uint64_t first_ref = pay + (src - src_orig);
                match_s = dub(src, first_ref, (upto - src), state_c.SMALL_BLOCK, &ref_s);

                if (match_s) {
                    n = 1;

                    while (true && match_s + (n + 1) * state_c.SMALL_BLOCK <= upto) {
                        uint64_t ref_s0 = 0;
                        auto m = dub(match_s + n * state_c.SMALL_BLOCK, pay + ((match_s + n * state_c.SMALL_BLOCK) - src_orig), 1, state_c.SMALL_BLOCK, &ref_s0);
                        if (ref_s0 + state_c.SMALL_BLOCK < first_ref && m == match_s + n * state_c.SMALL_BLOCK && ref_s0 == ref_s + n * state_c.SMALL_BLOCK) {
                            n++;
                        }
                        else {
                            break;
                        }
                    }
                }
            }

            if (n == 0) {
                dst += write_literals(src, upto - src + 1, dst, thread_id, false);
                break;
            }
            else {
                if (match_s - src > 0) {
                    dst += write_literals(src, match_s - src, dst, thread_id, false);
                }
                size_t min = minimum(n * state_c.SMALL_BLOCK, upto - match_s + 1);
                dst += write_match(min, ref_s, dst);
                src = match_s + min;
            }
        }

        if (match == 0) {
            return dst - dst_orig;
        }
        else {
            dst += write_match(minimum(state_c.LARGE_BLOCK, last - match + 1), ref, dst);
            src = match + state_c.LARGE_BLOCK;
        }
    }

    return dst - dst_orig;
}

static void* compress_thread(void* arg) {
    job_t* me = (job_t*)arg;

    for (;;) {
        pthread_mutex_lock_wrapper(&me->jobmutex);
        // Nothing to do, or waiting for consumer to fetch result
        while (!me->cancel && (me->size_source == 0 || me->size_destination > 0)) {
            pthread_cond_wait_wrapper(&me->cond, &me->jobmutex);
        }

        if(me->cancel) {
            pthread_mutex_unlock_wrapper(&me->jobmutex);
            return 0;
        }

        pthread_mutex_unlock_wrapper(&me->jobmutex);

        if (!me->entropy) {
            hash_chunk(me->source, me->payload, me->size_source);
            me->size_destination = process_chunk(me->source, me->payload, me->size_source, me->destination, me->id);
        }
        else {
            me->size_destination = write_literals(me->source, me->size_source, me->destination, me->id, true);
        }

        pthread_mutex_lock_wrapper(&me->jobmutex);
        me->busy = false;
        pthread_mutex_unlock_wrapper(&me->jobmutex);

        pthread_mutex_lock_wrapper(&state_c.jobdone_mutex);
        pthread_cond_signal_wrapper(&state_c.jobdone_cond);
        pthread_mutex_unlock_wrapper(&state_c.jobdone_mutex);
    }

    return 0;
}


static int get_free(void) {
    int i;
    for (i = 0; i < state_c.threads; i++) {
        pthread_mutex_lock_wrapper(&state_c.jobs[i].jobmutex);
        if (!state_c.jobs[i].busy && state_c.jobs[i].size_source == 0 && state_c.jobs[i].size_destination == 0) {
            return i;
        }

           pthread_mutex_unlock_wrapper(&state_c.jobs[i].jobmutex);
        
    }
    return -1;
}


static uint64_t packet_payload(const char* src) {
    uint64_t t = str2ll(src + 1 + 4 + 4, 8);
    return t;
}

// Public functions
/////////////////////////////////////////////////////////////////////////////////////

int dup_init_compression(size_t large_block, size_t small_block, uint64_t mem, int thread_count, void *space, int compression_level, bool crypto_hash, uint64_t hash_seed, uint64_t basepay) {
    assert(compression_level >= 0 && compression_level <= 3);
    assert(mem >= 1024 * 1024);
    assert(thread_count >= 1);
    assert(large_block > small_block);

    state_c.memsize = mem;
    state_c.global_payload = basepay;
    state_c.flushed = state_c.global_payload;
    state_c.g_crypto_hash = crypto_hash;
    state_c.g_hash_salt = hash_seed;
    state_c.level = compression_level;
    state_c.threads = thread_count;

    state_c.jobs = (job_t *)malloc(sizeof(job_t) * state_c.threads);
    if (!state_c.jobs) {
        return 1;
    }

    for (int i = 0; i < state_c.threads; i++) {
        (void)*(new (&state_c.jobs[i])(job_t)());
    }

#ifdef _WIN32
    pthread_win32_process_attach_np();
#endif
    pthread_mutex_init(&state_c.table_mutex, NULL);
    pthread_mutex_init(&state_c.jobdone_mutex, NULL);
    pthread_cond_init(&state_c.jobdone_cond, NULL);

    state_c.SMALL_BLOCK = small_block;
    state_c.LARGE_BLOCK = large_block;

    // Memory: begin, OVERHEAD, small blocks table, large blocks table, OVERHEAD, end
    memset(space, 0, mem);
    state_c.memory_begin = (char*)space;
    state_c.memory_end = state_c.memory_begin + mem;
    state_c.memory_table = state_c.memory_begin + COMPRESSED_HASHTABLE_OVERHEAD;
    size_t table_size = state_c.memory_end - state_c.memory_table - COMPRESSED_HASHTABLE_OVERHEAD;
    size_t total_blocks = table_size / sizeof(hashblock_t);
    state_c.large_entries = uint64_t(1. / float(state_c.size_ratio) * float(total_blocks));
    state_c.small_entries = total_blocks - state_c.large_entries;

    state_c.small_table = (hashblock_t*)state_c.memory_table;;
    state_c.large_table = state_c.small_table + state_c.small_entries;

    for (int i = 0; i < state_c.threads; i++) {
        pthread_mutex_init(&state_c.jobs[i].jobmutex, NULL);
        pthread_cond_init(&state_c.jobs[i].cond, NULL);
        state_c.jobs[i].id = i;
        state_c.jobs[i].zstd = zstd_init();
    }

    for (int i = 0; i < state_c.threads; i++) {
        int t = pthread_create(&state_c.jobs[i].thread, NULL, compress_thread, &state_c.jobs[i]);
        if (t) {
            return 2;
        }
    }
    return 0;
}

void dup_uninit_compression(void) {
    int i;
    for (i = 0; i < state_c.threads; i++) {
        pthread_mutex_lock_wrapper(&state_c.jobs[i].jobmutex);
        state_c.jobs[i].cancel = true;       
        ZSTD_freeDCtx(((zstd_params_s*)state_c.jobs[i].zstd)->dctx);
        ZSTD_freeCCtx(((zstd_params_s*)state_c.jobs[i].zstd)->cctx);
        free(state_c.jobs[i].zstd);
        state_c.jobs[i].zstd = 0;

        pthread_mutex_lock_wrapper(&state_c.jobdone_mutex);
        pthread_cond_signal_wrapper(&state_c.jobs[i].cond);
        pthread_mutex_unlock_wrapper(&state_c.jobdone_mutex);

        pthread_mutex_unlock_wrapper(&state_c.jobs[i].jobmutex);

        pthread_join(state_c.jobs[i].thread, 0);
    }

    if (state_c.jobs != 0) {
        free(state_c.jobs);
        state_c.jobs = 0;
    }
}

void dup_init_decompression() {
    state_d = (zstd_params_s *)zstd_init();
}

void dup_uninit_decompression() {
    ZSTD_freeCCtx(state_d->cctx);
    free(state_d);
}

size_t dup_compress_hashtable(char* dst) {
    char* dst_orig = dst;
    size_t total_entries = state_c.small_entries + state_c.large_entries;
    size_t s = sizeof(hashblock_t) * total_entries;
    uint64_t crc = hash64(&state_c.small_table[0], s);
    size_t block = 0;

    ll2str(crc, dst, 8);
    dst += 8;

    do {
        size_t count = equ(block);
        bool used = in_use(block);
        *dst++ = 'C';
        ll2str(count, dst, 8);
        ll2str(used ? 1 : 0, dst + 8, 1);
        dst += 9;
        for (size_t t = 0; t < count; t++) {
            size_t s = write_hashblock(&state_c.small_table[block], dst);
            dst += s;
            block++;
        }
    } while (block < total_entries);

    return dst - dst_orig;
}

// todo return 0 on error
int dup_decompress_hashtable(char* src) {
    size_t total_entries = state_c.small_entries + state_c.large_entries;
    size_t block = 0;
    uint64_t crc = str2ll(src, 8);
    src += 8;

    do {
        char b = *src++;
        if (b != 'C') {
            return 2;
        }
        uint64_t count = str2ll(src, 8);
        bool used = str2ll(src + 8, 1) != 0;
        src += 9;

        for (size_t h = 0; h < count; h++) {
            if (used) {
                size_t s = read_hashblock(&state_c.small_table[block], src);
                src += s;
            }
            else {
                for (size_t i = 0; i < SLOTS; i++) {
                    state_c.small_table[block].entry[i].offset = 0;
                    state_c.small_table[block].entry[i].slide = 0;
                    state_c.small_table[block].entry[i].first_byte = 0;
                    memset(&state_c.small_table[block].entry[i].sha, 0, HASH_SIZE);
                    state_c.small_table[block].hash[i] = 0;
                }
            }
            block++;
        }
    } while (block < total_entries);

    size_t s = sizeof(hashblock_t) * total_entries;
    // todo, add crc of compressed table too
    uint64_t crc2 = hash64(&state_c.small_table[0], s);
    return crc == crc2 ? 0 : 1;
}

bool packet_header(const char* src) {
    return src[0] == DUP_LITERAL || src[0] == DUP_REFERENCE;
}

size_t dup_size_compressed(const char *src) {
    if(!packet_header(src)) {
        return 0;
    }
    size_t t = str2ll(src + 1, 4);
    return t;
}

size_t dup_size_decompressed(const char *src) {
    if (!packet_header(src)) {
        return 0;
    }
    size_t t = str2ll(src + 1 + 4, 4);
    return t;
}

uint64_t dup_counter_payload(void) { return state_c.count_payload; }

void dup_counters_reset(void) {
    state_c.count_payload = 0;
}

// todo return 0 on error, else return DUP_REFERENCE/DUP_LITERAL
int dup_decompress(const char *src, char *dst, size_t *length, uint64_t *payload) {
    if (src[0] == DUP_LITERAL) {
        size_t t;
        char level = src[DUP_HEADER_LEN];

        if (level == '0') {
            t = dup_size_decompressed(src);
            memcpy(dst, src + 1 + DUP_HEADER_LEN, t);
        } else if (level == '1' || level == '2' || level == '3') {
            size_t len_de = dup_size_decompressed(src);
            size_t len = dup_size_compressed(src) - DUP_HEADER_LEN - 1;
            t = zstd_decompress((char *)src + DUP_HEADER_LEN + 1, len, dst, len_de, 0, 0, state_d);
            t = len_de;
        } else {
            return -1;
        }

        *length = t;
        state_c.count_payload += *length;
        return 0;
    }
    else if (src[0] == DUP_REFERENCE) {
        uint64_t pay = packet_payload(src);
        size_t len = dup_size_decompressed(src);
        *payload = pay;
        *length = len;
        state_c.count_payload += *length;
        return 1;
    } else {
        return -2;
    }
}
// todo, 0 on error
int dup_packet_info(const char *src, size_t *length, uint64_t *payload) {
    if (src[0] == DUP_LITERAL) {
        size_t len = dup_size_decompressed(src);
        if (len == 0) {
            return -1;
        }
        if(length) {
            *length = len;
        }
        return DUP_LITERAL;
    }
    else if (src[0] == DUP_REFERENCE) {
        uint64_t pay = packet_payload(src);
        size_t len = dup_size_decompressed(src);
        if(payload) {
            *payload = pay;
        }
        if (len == 0) {
            return -1;
        }
        if(length) {
            *length = len;
        }
        return DUP_REFERENCE;
    }
    return -2;
}

// Return value: packet size
size_t dup_flush_pend(uint64_t* payload_size, char** packet, uint64_t* payload_counter) {
    size_t res = 0;
    if(payload_size) {
        *payload_size = 0;
    }
    if (payload_counter) {
        *payload_counter = 0;
    }
    int i;
    for (i = 0; i < state_c.threads; i++) {
        pthread_mutex_lock_wrapper(&state_c.jobs[i].jobmutex);
        if (!state_c.jobs[i].busy && state_c.jobs[i].size_destination > 0 && state_c.jobs[i].payload == state_c.flushed) {
            if(payload_counter) {
                *payload_counter = state_c.jobs[i].payload;
            }
            if(packet) {
                *packet = state_c.jobs[i].destination;
            }
            if(payload_size) {
                *payload_size = state_c.jobs[i].size_source;
            }
            res = state_c.jobs[i].size_destination;
            state_c.flushed += state_c.jobs[i].size_source;
            state_c.jobs[i].size_destination = 0;
            state_c.jobs[i].size_source = 0;
            pthread_mutex_unlock_wrapper(&state_c.jobs[i].jobmutex);
            break;
        }
        pthread_mutex_unlock_wrapper(&state_c.jobs[i].jobmutex);
    }
    return res;
}

// Return value: packet size
size_t dup_flush_pend_block(uint64_t* payload_size, char** packet, uint64_t* payload_counter) {
    if(payload_size) {
        *payload_size = 0;
    }
    if(payload_counter) {
        *payload_counter = 0;
    }
    if (state_c.flushed == state_c.count_payload) {
        return 0;
    }
    for (;;) {
        size_t r = dup_flush_pend(payload_size, packet, payload_counter);
        if (r) {
            return r;
        }
        sched_yield_wrapper();
    }
}
// (src, size): 'packet' of payload you want to compress
// dst: point to where you want the compressed result of this particular packet written to
// if return value > 0: a compressed packet was finished and written to 'packet', and compressed
// size equals return value. 'packet' will equal one of the 'dst' pointers you passed earlier.
size_t dup_compress(const void *src, char *dst, size_t size, uint64_t *payload_size, bool entropy, char**packet, uint64_t* payload_counter) {
    size_t ret = 0;
    if(payload_size) {
        *payload_size = 0;
    }

    if (size > 0) {
        // todo, re-create the non-threaded blocking version here
        int f = -1;
        pthread_mutex_lock(&state_c.jobdone_mutex);
        do {
            ret = dup_flush_pend(payload_size, packet, payload_counter);
            f = get_free();
            if (f == -1) {
                pthread_cond_wait(&state_c.jobdone_cond, &state_c.jobdone_mutex);
            }
        } while (f == -1);

        state_c.jobs[f].busy = true;
        state_c.jobs[f].payload = state_c.global_payload;
        state_c.global_payload += size;
        state_c.count_payload += size;
        state_c.jobs[f].size_source = size;
        state_c.jobs[f].entropy = entropy;
        state_c.jobs[f].source = (char*)src;
        state_c.jobs[f].destination = (char*)dst;

        pthread_cond_signal_wrapper(&state_c.jobs[f].cond);
        pthread_mutex_unlock_wrapper(&state_c.jobs[f].jobmutex);
        pthread_mutex_unlock_wrapper(&state_c.jobdone_mutex);
    }
    return ret;
}