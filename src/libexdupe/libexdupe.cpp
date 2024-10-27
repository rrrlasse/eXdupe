// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2023: Lasse Mikkel Reinhold

#define HASH_SIZE 16

#include <assert.h>
#include <immintrin.h>
#include <smmintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "pthread/pthread.h"
#include <Windows.h>
#include <intrin.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include <condition_variable>
#include <iostream>
#include <vector>

#include "blake3/c/blake3.h"
#include "xxHash/xxh3.h"
#include "xxHash/xxhash.h"
#include "../gsl/gsl"

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd/lib/zstd.h"
#include "../error_handling.h"

#include "libexdupe.h"

// #define EXDUPE_THREADTEST

#ifdef EXDUPE_THREADTEST
void threadtest_delay(void) {
    const uint64_t delay_frequency = 100;
    static std::atomic<uint64_t> r = 0;
    const uint64_t multiplier = 0xc7d7ecef3d0a1f23;
    const uint64_t increment = 0xb4e3a3eb07c057d1;

    r = (multiplier * r + increment);

    if (r < 0xffffffffffffffff / delay_frequency) {
        if (r % 10000 == 0) {
            Sleep(r % 1000);
        } else if (r % 1000 == 0) {
            Sleep(r % 100);
        } else {
            Sleep(r % 10);
        }
    }
}
#else
void threadtest_delay(void) {}
#endif

int pthread_mutex_lock_wrapper(pthread_mutex_t *m) {
    threadtest_delay();
    int r = pthread_mutex_lock(m);
    threadtest_delay();
    return r;
}

int pthread_mutex_unlock_wrapper(pthread_mutex_t *m) {
    threadtest_delay();
    int r = pthread_mutex_unlock(m);
    threadtest_delay();
    return r;
}

int pthread_cond_wait_wrapper(pthread_cond_t *c, pthread_mutex_t *m) {
    threadtest_delay();
    int r = pthread_cond_wait(c, m);
    threadtest_delay();
    return r;
}

int pthread_cond_broadcast_wrapper(pthread_cond_t *c) {
    threadtest_delay();
    int r = pthread_cond_broadcast(c);
    threadtest_delay();
    return r;
}

int pthread_cond_signal_wrapper(pthread_cond_t *c) {
    threadtest_delay();
    int r = pthread_cond_signal(c);
    threadtest_delay();
    return r;
}
int pthread_mutex_trylock_wrapper(pthread_mutex_t *mutex) {
    threadtest_delay();
    int r = pthread_mutex_trylock(mutex);
    threadtest_delay();
    return r;
}

namespace {

bool g_crypto_hash = false;
uint64_t g_hash_salt = 0;
pthread_mutex_t table_mutex;
pthread_cond_t jobdone_cond;
pthread_mutex_t jobdone_mutex;

int threads;
int level;

// When compressing the hashtable, worst case is that all entries are in use, in which case it ends up
// growing COMPRESSED_HASHTABLE_OVERHEAD bytes in size.
#define COMPRESSED_HASHTABLE_OVERHEAD 4096

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

const uint64_t slots = 8;

#pragma pack(push, 1)

struct hash_t {
    uint64_t offset;
    uint16_t slide;
    char sha[HASH_SIZE];
    uint8_t first_byte;
};

struct hashblock_t {
    uint32_t hash[slots];
    hash_t entry[slots];
};

#pragma pack(pop)

size_t SMALL_BLOCK;
size_t LARGE_BLOCK;

const uint64_t size_ratio = 32;
uint64_t small_entries;
uint64_t large_entries;


hashblock_t *small_table;
hashblock_t *large_table;
}

char* memory_begin;
char* memory_table;
char* memory_end;

size_t memsize;


// statistics
std::atomic<uint64_t> largehits;
std::atomic<uint64_t> smallhits;
std::atomic<uint64_t> stored_as_literals;
std::atomic<uint64_t> literals_compressed_size;
std::atomic<uint64_t> anomalies_small;
std::atomic<uint64_t> anomalies_large;
std::atomic<uint64_t> congested_small;
std::atomic<uint64_t> congested_large;
std::atomic<uint64_t> high_entropy;

std::atomic<uint64_t> hits1;
std::atomic<uint64_t> hits2;
std::atomic<uint64_t> hits3;
std::atomic<uint64_t> hits4;

size_t write_hashblock(hashblock_t* h, char* dst) {
    char* orig_dst = dst;
    size_t t;

    if (h->hash[0] == 0) {
        return 0;
    }

    for (t = 0; t < slots; t++) {

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

size_t read_hashblock(hashblock_t* h, char* src) {
    char* orig_src = src;
    bool nulls = false;
    for (size_t t = 0; t < slots; t++) {

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

void print_hashblock(hashblock_t* h) {
    for (size_t t = 0; t < slots; t++) {
        std::wcerr << L"(" << std::hex << h->hash[t] << std::dec << L"," << h->entry[t].offset << L"," << h->entry[t].slide << L"," << (int)h->entry[t].sha[0] << L") \n";
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


hash_t* lookup(uint32_t hash, bool large) {
    hashblock_t* table = (large ? large_table : small_table);
    uint32_t row = hash % ((large ? large_entries : small_entries));
    hashblock_t& e = table[row];

    for(uint64_t i = 0; i < slots; i++) {
        if (e.hash[i] == 0) {
            return 0;
        }
        else if(e.hash[i] == hash && hash != 0) {
            return &e.entry[i];
        }
    }
    return 0;
}


bool add(hash_t value, uint32_t hash, bool large) {
    hashblock_t* table = (large ? large_table : small_table);
    uint32_t row = hash % ((large ? large_entries : small_entries));
    hashblock_t& e = table[row];

    for(uint64_t i = 0; i < slots; i++) {
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

}

template <class T> const T minimum(const T a, const T b) {
    return a > b ? b : a;
}

typedef struct {
    pthread_t thread;
    int status;

    char* source;
    char* destination;
    size_t source_capacity;

    uint64_t payload;
    size_t size_source;
    size_t size_destination;
    pthread_mutex_t jobmutex;
    pthread_cond_t cond;
    int id;
    char *zstd;
    bool busy;

    bool entropy; // .jpg, .mpeg, .zip etc
} job_t;

job_t *jobs;

char *zstd_decompress_state;

typedef struct {
    ZSTD_CCtx *cctx;
    ZSTD_DCtx *dctx;
    ZSTD_CDict *cdict;
    ZSTD_parameters zparams;
    ZSTD_customMem cmem;
} zstd_params_s;

char *zstd_init() {
    // Todo, leaks if we should one day decide to use thread cancelation
    // (bool exit_threads).
    zstd_params_s *zstd_params = (zstd_params_s *)malloc(sizeof(zstd_params_s));
    if (!zstd_params) {
        return NULL;
    }
    zstd_params->cctx = ZSTD_createCCtx();
    zstd_params->dctx = ZSTD_createDCtx();
    zstd_params->cdict = NULL;
    return (char *)zstd_params;
}


int64_t zstd_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, int level, char *workmem) {
    zstd_params_s *zstd_params = (zstd_params_s *)workmem;
    size_t ret = ZSTD_compressCCtx(zstd_params->cctx, outbuf, outsize, inbuf, insize, level);
    return ret;
}


bool is_compressible(char* inbuf, size_t insize, char* outbuf, char* workmem) {
    zstd_params_s* zstd_params = (zstd_params_s*)workmem;
    size_t ret = 0;
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


int64_t zstd_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char *workmem) {
    zstd_params_s *zstd_params = (zstd_params_s *)workmem;
    return ZSTD_decompressDCtx(zstd_params->dctx, outbuf, outsize, inbuf, insize);
}

static void hash(const void *src, size_t len, uint64_t salt, char *dst) {
    if (g_crypto_hash) {
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
    for (uint64_t i = 0; i < small_entries; i++) {
        print_hashblock(&small_table[i]);
    }
    std::wcerr << L"\nlarge:\n";
    for (uint64_t i = 0; i < large_entries; i++) {
        print_hashblock(&large_table[i]);
    }
    std::wcerr << "\n\n";
#endif
}

bool in_use(size_t i) {
    for(size_t t = 0; t < slots; t++) {
        if(small_table[i].hash[t] != 0) {
            return true;
        }
    }
    return false;
}

size_t equ(size_t start) {
    bool used = in_use(start);
    size_t count = 1;
    size_t total_entries = small_entries + large_entries;
    while(start + count < total_entries) {
        if(used != in_use(start + count)) {
            break;
        }
        count++;
    }
    return count;
}

size_t dup_compress_hashtable(char* dst) {
    char* dst_orig = dst;
    size_t total_entries = small_entries + large_entries;
    size_t s = sizeof(hashblock_t) * total_entries;
    uint64_t crc = hash64(&small_table[0], s);
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
        for(size_t t = 0; t < count; t++) {
            size_t s = write_hashblock(&small_table[block], dst);
            dst += s;
            block++;
        }
    } while (block < total_entries);

    return dst - dst_orig;
}

int dup_decompress_hashtable(char* src) {
    size_t total_entries = small_entries + large_entries;
    size_t block = 0;
    uint64_t crc = str2ll(src, 8);
    src += 8;

    do {
        char b = *src++;
        if(b != 'C') {
            return 2;
        }
        uint64_t count = str2ll(src, 8);
        bool used = str2ll(src + 8, 1) != 0;
        src += 9;

        for (size_t h = 0; h < count; h++) {
            if(used) {
                size_t s = read_hashblock(&small_table[block], src);
                src += s;
            }
            else {
                for(size_t i = 0; i < slots; i++) {
                    small_table[block].entry[i].offset = 0;
                    small_table[block].entry[i].slide = 0;
                    small_table[block].entry[i].first_byte = 0;
                    memset(&small_table[block].entry[i].sha, 0, HASH_SIZE);
                    small_table[block].hash[i] = 0;
                }
            }
            block++;
        }
    } while(block < total_entries);

    size_t s = sizeof(hashblock_t) * total_entries;
    // todo, add crc of compressed table too
    uint64_t crc2 = hash64(&small_table[0], s);
    return crc == crc2 ? 0 : 1;
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
    size_t slide = minimum<size_t>(len / 2, 65536); // slide must be able to fit in hash_t.O. Todo, static assert
    size_t block = len - slide;
    int16_t b = len >= LARGE_BLOCK ? 32767 - 32 : 32767 - 256;
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
            unsigned int off = minimum(off1, off2);
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

// there must be LARGE_BLOCK more valid data after src + len
const static char *dub(const char *src, uint64_t pay, size_t len, size_t block, uint64_t *payload_ref) {
    rassert(block == LARGE_BLOCK || block == SMALL_BLOCK);
    const char *w_pos;
    const char *orig_src = src;
    const char *last_src = src + len - 1;
    uint32_t w = window(src, block, &w_pos);
    const char* collision = 0;

    while (src <= last_src) {
        hash_t* e;
        // CAUTION: Outside mutex, assume reading garbage and that data changes between reads
        if (w != 0 && (e = lookup(w, block == LARGE_BLOCK))) {
            hash_t e_cpy;
            pthread_mutex_lock_wrapper(&table_mutex);
            e = lookup(w, block == LARGE_BLOCK);
            if(e) {
                e_cpy = *e;
            }
            pthread_mutex_unlock_wrapper(&table_mutex);
            if (e && w_pos - e_cpy.slide > src && w_pos - e_cpy.slide <= last_src) {
                src = w_pos - e_cpy.slide;
            }

            if (e && e_cpy.first_byte == (uint8_t)src[0] && e_cpy.offset + block < pay + (src - orig_src)) {
                char s[HASH_SIZE];

                if (block == LARGE_BLOCK) {
                    char tmp[8 * 1024];
                    rassert(sizeof(tmp) >= LARGE_BLOCK / SMALL_BLOCK * HASH_SIZE);
                    uint32_t k;
                    for (k = 0; k < LARGE_BLOCK / SMALL_BLOCK; k++) {
                        hash(src + k * SMALL_BLOCK, SMALL_BLOCK, g_hash_salt, tmp + k * HASH_SIZE);
                    }
                    hash(tmp, LARGE_BLOCK / SMALL_BLOCK * HASH_SIZE, g_hash_salt, s);
                } else {
                    hash(src, block, g_hash_salt, s);
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
            w = window(src, block, &w_pos);
        }
    }

    return 0;
}

static bool hashat(const char *src, uint64_t pay, size_t len, bool large, char *hash) {
    const char *o;
    uint32_t w = window(src, len, &o);
    if(w != 0) {
        pthread_mutex_lock_wrapper(&table_mutex);
        hash_t e;            
        e.first_byte = (uint8_t)src[0];
        e.offset = pay;
        memcpy(e.sha, hash, HASH_SIZE);
        e.slide = static_cast<uint16_t>(o - src);
        bool added = add(e, w, large);
        if(!added) {
            if(large) {
                congested_large += len;
            }
            else {
                congested_small += len;
            }
        }
        pthread_mutex_unlock_wrapper(&table_mutex);
        return true;
    }
    else {
        return false;
    }
}

static size_t write_match(size_t length, uint64_t payload, char *dst) {
   // wcerr << L"match = " << length << L"," << payload << L"\n";
    if (length > 0) {
        if(length == LARGE_BLOCK) {
            largehits += length;
        }
        else {
            smallhits += length;
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
    rassert(level >= 0 && level <= 3);

    if (length > 0) {
        size_t packet_size = 0;
        bool compressible = true;
        
        if(!entropy) {
            compressible = is_compressible((char*)src, length, dst, jobs[thread_id].zstd);
        }

        if (level == 0 || entropy || !compressible) {
            dst[DUP_HEADER_LEN] = '0';
            memcpy(dst + DUP_HEADER_LEN + 1, src, length);
            // DUP_HEADER, '0', raw data
            packet_size = 1 + length;
        } else if (level >= 1 && level <= 3) {
            int zstd_level = level == 1 ? 1 : level == 2 ? 10 : 19;
            dst[DUP_HEADER_LEN] = char(level + '0');
            int64_t r = zstd_compress((char *)src, length, dst + DUP_HEADER_LEN + 1, 2 * length + 1000000, zstd_level, jobs[thread_id].zstd);
            packet_size = 1 + r;
        }

        packet_size += DUP_HEADER_LEN;

        dst[0] = DUP_LITERAL;
        ll2str(packet_size, dst + 1, 4);
        ll2str(length, dst + 5, 4);
        ll2str(0, dst + 9, 8);

        stored_as_literals += length;
        literals_compressed_size += packet_size;

        return packet_size;
    }
    return 0;
}

static void hash_chunk(const char *src, uint64_t pay, size_t length) {
    char tmp[512 * HASH_SIZE];
    rassert(sizeof(tmp) >= HASH_SIZE * LARGE_BLOCK / SMALL_BLOCK);

    size_t small_blocks = length / SMALL_BLOCK;
    uint32_t smalls = 0;
    uint32_t j = 0;

    for (j = 0; j < small_blocks; j++) {
        hash(src + j * SMALL_BLOCK, SMALL_BLOCK, g_hash_salt, tmp + smalls * HASH_SIZE);
        bool success_small = hashat(src + j * SMALL_BLOCK, pay + j * SMALL_BLOCK, SMALL_BLOCK, false, tmp + smalls * HASH_SIZE);
        if(!success_small) {
            anomalies_small += SMALL_BLOCK;
        }

        smalls++;
        if (smalls == LARGE_BLOCK / SMALL_BLOCK) {
            char tmp2[HASH_SIZE];
            hash(tmp, smalls * HASH_SIZE, g_hash_salt, tmp2);
            bool success_large = hashat(src + (j + 1) * SMALL_BLOCK - LARGE_BLOCK, pay + (j + 1) * SMALL_BLOCK - LARGE_BLOCK, LARGE_BLOCK, true, tmp2);
            if(!success_large) {
                anomalies_large += SMALL_BLOCK;
            }

            smalls = 0;
        }
    }
    return;
}


static size_t process_chunk(const char* src, uint64_t pay, size_t length, char* dst, int thread_id) {
    const char* upto;
    const char* src_orig = src;
    char* dst_orig = dst;
    const char* last = src + length - 1;

    while (src <= last) {
        uint64_t ref = 0;
        const char* match = 0;

        if (src + LARGE_BLOCK - 1 <= last) {
            match = dub(src, pay + (src - src_orig), last - src, LARGE_BLOCK, &ref);
        }
        upto = (match == 0 ? last : match - 1);

        while (src <= upto) {
            uint64_t ref_s = 0;
            const char* match_s = 0;
            size_t n = 0;

            if (src + SMALL_BLOCK - 1 <= upto) {
                uint64_t first_ref = pay + (src - src_orig);
                match_s = dub(src, first_ref, (upto - src), SMALL_BLOCK, &ref_s);

                if (match_s) {
                    n = 1;

                    while (true && match_s + (n + 1) * SMALL_BLOCK <= upto) {
                        uint64_t ref_s0 = 0;
                        auto m = dub(match_s + n * SMALL_BLOCK, pay + ((match_s + n * SMALL_BLOCK) - src_orig), 1, SMALL_BLOCK, &ref_s0);
                        if (ref_s0 + SMALL_BLOCK < first_ref && m == match_s + n * SMALL_BLOCK && ref_s0 == ref_s + n * SMALL_BLOCK) {
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
                auto min = minimum<size_t>(n * SMALL_BLOCK, upto - match_s + 1);
                dst += write_match(min, ref_s, dst);
                src = match_s + min;
            }
        }

        if (match == 0) {
            return dst - dst_orig;
        }
        else {
            dst += write_match(minimum<size_t>(LARGE_BLOCK, last - match + 1), ref, dst);
            src = match + LARGE_BLOCK;
        }
    }

    return dst - dst_orig;
}


// Public interface
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint64_t flushed;
uint64_t global_payload;
uint64_t count_payload;

static int get_free(void) {
    int i;
    for (i = 0; i < threads; i++) {
        pthread_mutex_lock_wrapper(&jobs[i].jobmutex);
        if (!jobs[i].busy && jobs[i].size_source == 0 && jobs[i].size_destination == 0) {
            return i;
        }
        pthread_mutex_unlock_wrapper(&jobs[i].jobmutex);
    }
    return -1;
}

static void *compress_thread(void *arg) {
    job_t *me = (job_t *)arg;

    for(;;) {
        pthread_mutex_lock_wrapper(&me->jobmutex);
        // Nothing to do, or waiting for consumer to fetch result
        while (me->size_source == 0 || me->size_destination > 0) {
            pthread_cond_wait_wrapper(&me->cond, &me->jobmutex);
        }

        me->busy = true;
        pthread_mutex_unlock_wrapper(&me->jobmutex);

       // me->size_destination = process_chunk(me->source, me->payload, me->size_source, me->destination, me->id);
       // hash_chunk(me->source, me->payload, me->size_source, policy);

        if(!me->entropy) {
            hash_chunk(me->source, me->payload, me->size_source);
//            auto t = GetTickCount();
            me->size_destination = process_chunk(me->source, me->payload, me->size_source, me->destination, me->id);
//            hits1 += GetTickCount() - t;
        }
        else {
            me->size_destination = write_literals(me->source, me->size_source, me->destination, me->id, true);
        }

        pthread_mutex_lock_wrapper(&me->jobmutex);
        me->busy = false;
        pthread_mutex_unlock_wrapper(&me->jobmutex);

        pthread_mutex_lock_wrapper(&jobdone_mutex);
        pthread_cond_signal_wrapper(&jobdone_cond);
        pthread_mutex_unlock_wrapper(&jobdone_mutex);
    }

    return 0;
}


int dup_init(size_t large_block, size_t small_block, uint64_t mem, int thread_count, void *space, int compression_level, bool crypto_hash, uint64_t hash_seed, uint64_t basepay) {
    // FIXME: The dup() function contains a stack allocated array ("tmp") of 8
    // KB that must be able to fit LARGE_BLOCK / SMALL_BLOCK * HASH_SIZE bytes.
    // Find a better solution. alloca() causes sporadic crash in VC for inlined
    // functions.
    //assert(large_block <= 512 * 1024);

    memsize = mem;

    count_payload = 0;
    global_payload = basepay;
    flushed = global_payload;

    g_crypto_hash = crypto_hash;
    g_hash_salt = hash_seed;
    level = compression_level;
    threads = thread_count;

    jobs = (job_t *)malloc(sizeof(job_t) * threads);
    if (!jobs) {
        return 1;
    }

    memset(jobs, 0, sizeof(job_t) * threads);

    for (int i = 0; i < threads; i++) {
        (void)*(new (&jobs[i])(job_t)());
    }

#ifdef _WIN32
    pthread_win32_process_attach_np();
#endif

    pthread_mutex_init(&table_mutex, NULL);

    pthread_mutex_init(&jobdone_mutex, NULL);
    pthread_cond_init(&jobdone_cond, NULL);

    SMALL_BLOCK = small_block;
    LARGE_BLOCK = large_block;

    // Memory: begin, OVERHEAD, small blocks table, large blocks table, OVERHEAD, end

    memory_begin = (char*)space;
    memory_end = memory_begin + mem;
    memory_table = memory_begin + COMPRESSED_HASHTABLE_OVERHEAD;

    size_t table_size = memory_end - memory_table - COMPRESSED_HASHTABLE_OVERHEAD;
    size_t total_blocks = table_size / sizeof(hashblock_t);
    large_entries = uint64_t(1. / float(size_ratio) * float(total_blocks));
    small_entries = total_blocks - large_entries;

    small_table = (hashblock_t*)memory_table;;
    large_table = small_table + small_entries;

    memset(space, 0, mem);

    for (int i = 0; i < threads; i++) {
        pthread_mutex_init(&jobs[i].jobmutex, NULL);
        pthread_cond_init(&jobs[i].cond, NULL);
        jobs[i].id = i;
        jobs[i].size_destination = 0;
        jobs[i].size_source = 0;
        jobs[i].zstd = zstd_init();

        jobs[i].busy = false;

        jobs[i].source = 0;
        jobs[i].destination = 0;
        jobs[i].source_capacity = 0;
    }

    for (int i = 0; i < threads; i++) {
        int t = pthread_create(&jobs[i].thread, NULL, compress_thread, &jobs[i]);
        if (t) {
            return 2;
        }
    }

#if 0
	cerr << "\nHASH ENTRIES = " << total_entries << "\n";
	cerr << "\nHASH SIZE = " << sizeof(hash_t) << "\n";
#endif

    return 0;
}

void dup_deinit(void) {
    int i;
    for (i = 0; i < threads; i++) {
        pthread_mutex_lock_wrapper(&jobs[i].jobmutex);
        pthread_cond_signal_wrapper(&jobs[i].cond);
        pthread_mutex_unlock_wrapper(&jobs[i].jobmutex);
        pthread_join(jobs[i].thread, 0);
    }

    if (jobs != 0) {
        free(jobs);
    }
}

size_t dup_size_compressed(const char *src) {
    // see "packet format" at the top of libexdupe.h
    size_t t = str2ll(src + 1, 4);
    return t;
}

size_t dup_size_decompressed(const char *src) {
    // see "packet format" at the top of libexdupe.h
    size_t t = str2ll(src + 1 + 4, 4);
    return t;
}

uint64_t dup_counter_payload(void) { return count_payload; }

void dup_counters_reset(void) {
    count_payload = 0;
}

static uint64_t packet_payload(const char *src) {
    // see "packet format" at the top of libexdupe.h
    uint64_t t = str2ll(src + 1 + 4 + 4, 8);
    return t;
}

int dup_decompress(const char *src, char *dst, size_t *length, uint64_t *payload) {
    if (zstd_decompress_state == 0) {
        // todo
        zstd_decompress_state = zstd_init();
    }

    if (src[0] == DUP_LITERAL) {
        size_t t;
        char level = src[DUP_HEADER_LEN];

        if (level == '0') {
            t = dup_size_decompressed(src);
            memcpy(dst, src + 1 + DUP_HEADER_LEN, t);
        } else if (level == '1' || level == '2' || level == '3') {
            int32_t len_de = dup_size_decompressed(src);
            int32_t len = dup_size_compressed(src) - DUP_HEADER_LEN - 1;
            t = zstd_decompress((char *)src + DUP_HEADER_LEN + 1, len, dst, len_de, 0, 0, zstd_decompress_state);
            t = len_de;
        } else {
            return -1;
        }

        *length = t;
        count_payload += *length;
        return 0;
    }
    else if (src[0] == DUP_REFERENCE) {
        uint64_t pay = packet_payload(src);
        size_t len = dup_size_decompressed(src);
        *payload = pay;
        *length = len;
        count_payload += *length;
        return 1;
    } else {
        return -2;
    }
}


int dup_packet_info(const char *src, size_t *length, uint64_t *payload) {
    if (src[0] == DUP_LITERAL) {
        size_t len = dup_size_decompressed(src);
        if (len == 0) {
            return -1;
        }
        *length = len;
        return DUP_LITERAL;
    }
    else if (src[0] == DUP_REFERENCE) {
        uint64_t pay = packet_payload(src);
        size_t len = dup_size_decompressed(src);
        *payload = pay;
        if (len == 0) {
            return -1;
        }
        *length = len;
        return DUP_REFERENCE;
    }
    return -2;
}

size_t flush_pend(uint64_t *payloadret, char*&retval_start) {
    size_t res = 0;
    *payloadret = 0;
    int i;
    for (i = 0; i < threads; i++) {
        pthread_mutex_lock_wrapper(&jobs[i].jobmutex);
        if (!jobs[i].busy && jobs[i].size_destination > 0 && jobs[i].payload == flushed) {

            res = jobs[i].size_destination;
            retval_start = jobs[i].destination;
            flushed += jobs[i].size_source;
            jobs[i].size_destination = 0;
            *payloadret = jobs[i].size_source;
            jobs[i].size_source = 0;
            pthread_mutex_unlock_wrapper(&jobs[i].jobmutex);
            break;
        }
        pthread_mutex_unlock_wrapper(&jobs[i].jobmutex);
    }
    return res;
}

uint64_t dup_get_flushed() { return flushed; }

// (src, size): 'packet' of payload you want to compress
// dst: point to where you want the compressed result of this particular packet written to
// if return value > 0: a compressed packet was finished and written to retval_start, and compressed
// size equals return value. retval_start will equal one of the 'dst' pointers you passed earlier.
size_t dup_compress(const void *src, char *dst, size_t size, uint64_t *payload_returned, bool entropy, char*&retval_start) {
    size_t ret = 0;
    *payload_returned = 0;

    if(entropy) {
        high_entropy += size;
    }


#if 0 // single threaded naive for debugging
	if (size > 0)
	{
		size_t t = process_chunk((char*)src, global_payload, size, (char*)dst, 1);
		if (add_data) {
			hash_chunk((char*)src, global_payload, size);
		}
		global_payload += size;
		return t;
	}
	else
		return 0;
#endif

    if (size > 0) {
        int f = -1;
        pthread_mutex_lock_wrapper(&jobdone_mutex);
        do {
            ret = flush_pend(payload_returned, retval_start);
            f = get_free();

           // assert(!(dst != dst_orig && f == -1));

            if (f == -1) {
                pthread_cond_wait_wrapper(&jobdone_cond, &jobdone_mutex);
            }
        } while (f == -1);

        jobs[f].payload = global_payload;
        global_payload += size;
        count_payload += size;
        jobs[f].size_source = size;
        jobs[f].entropy = entropy;
        jobs[f].source = (char*)src;
        jobs[f].destination = (char*)dst;

        pthread_cond_signal_wrapper(&jobs[f].cond);
        pthread_mutex_unlock_wrapper(&jobs[f].jobmutex);
        pthread_mutex_unlock_wrapper(&jobdone_mutex);
    }

    return ret;
}
