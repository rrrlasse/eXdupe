// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2023: Lasse Mikkel Reinhold

#if defined _MSC_VER
#include <intrin.h>
#endif

// #define HASH_PARTIAL_BLOCKS

#define HASH_SIZE 16

#if defined(__SVR4) && defined(__sun)
#include <thread.h>
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64)
#define WINDOWS
#endif

#if (defined(__X86__) || defined(__i386__) || defined(i386) || defined(_M_IX86) || defined(__386__) || defined(__x86_64__) || defined(_M_X64))
#define X86X64
#endif

#include <assert.h>
#include <immintrin.h>
#include <smmintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WINDOWS
#include "pthread/pthread.h"
#include <Windows.h>
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

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd/lib/zstd.h"

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
std::mutex job_info;

int threads;
int level;

// When compressing the hashtable, worst case is that all entries are in use, in which case it ends up
// growing COMPRESSED_HASHTABLE_OVERHEAD bytes in size.
#define COMPRESSED_HASHTABLE_OVERHEAD 4096

#define DUP_MATCH "MM"
#define DUP_LITERAL "TT"

static void ll2str(uint64_t l, char* dst, int bytes) {
    while (bytes > 0) {
        *dst = (l & 0xff);
        dst++;
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
    unsigned char sha[HASH_SIZE];
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
        memcpy(dst + 14, h->entry[t].sha, HASH_SIZE);
        dst += 14 + HASH_SIZE;
    }
    return dst - orig_dst;
}

size_t read_hashblock(hashblock_t* h, char* src) {
    char* orig_src = src;
    bool nulls = false;
    for (size_t t = 0; t < slots; t++) {

        if (!nulls) {
            h->hash[t] = str2ll(src, 4);
            src += 4;
        }

        if (h->hash[t] == 0) {
            nulls = true;
        }

        if (nulls) {
            h->hash[t] = 0;
            h->entry[t].offset = 0;
            h->entry[t].slide = 0;
            memset(h->entry[t].sha, 0, HASH_SIZE);
        }
        else {
            h->entry[t].offset = str2ll(src, 8);
            h->entry[t].slide = str2ll(src + 8, 2);
            memcpy(h->entry[t].sha, src + 10, HASH_SIZE);
            src += 10 + HASH_SIZE;
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

template <class T, class U> const uint64_t minimum(const T a, const U b) {
    return (static_cast<uint64_t>(a) > static_cast<uint64_t>(b)) ? static_cast<uint64_t>(b) : static_cast<uint64_t>(a);
}

typedef struct {
    pthread_t thread;
    int status;

    unsigned char* source;
    unsigned char* destination;
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
    size_t ret = 0;
    zstd_params_s *zstd_params = (zstd_params_s *)workmem;
    int fast = 2048;
    int slow = 4096;

    if(insize > 128*1024) {
        if( 
            ZSTD_compressCCtx(zstd_params->cctx, outbuf, outsize, inbuf + insize / 2, fast, level) >= fast &&
            ZSTD_compressCCtx(zstd_params->cctx, outbuf, outsize, inbuf + insize / 3 * 0, slow, level) >= slow &&
            ZSTD_compressCCtx(zstd_params->cctx, outbuf, outsize, inbuf + insize / 3 * 1, slow, level) >= slow &&
            ZSTD_compressCCtx(zstd_params->cctx, outbuf, outsize, inbuf + insize / 3 * 2, slow, level) >= slow &&
            ZSTD_compressCCtx(zstd_params->cctx, outbuf, outsize, inbuf + insize - slow, slow, level) >= slow &&
            true) {
            *outbuf++ = 'U';
            ret++;
            memcpy(outbuf, inbuf, insize);
            return insize + 1;
        }            
    }    

    *outbuf++ = 'C';
    ret++;

    ret += ZSTD_compressCCtx(zstd_params->cctx, outbuf, outsize, inbuf, insize, level);
    assert(!ZSTD_isError(res));
    return ret;
}

int64_t zstd_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char *workmem) {
    char compressible = *inbuf++;

    if(compressible == 'U') {
        memcpy(outbuf, inbuf, insize);
        return insize;
    }
    else if(compressible == 'C') {
        zstd_params_s *zstd_params = (zstd_params_s *)workmem;
        return ZSTD_decompressDCtx(zstd_params->dctx, outbuf, outsize, inbuf, insize);
    }
    
    return -1;
}

static void hash(const void *src, size_t len, uint64_t salt, unsigned char *dst) {
    if (g_crypto_hash) {
        char s[sizeof(salt)];
        ll2str(salt, s, sizeof(salt));
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
    unsigned char h[HASH_SIZE];
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
    size_t total_used = 0;

    ll2str(crc, dst, 8);
    dst += 8;

    do {
        size_t count = equ(block);
        bool used = in_use(block);
        *dst++ = 'C';
        ll2str(count, dst, 8);
        ll2str(used ? 1 : 0, dst + 8, 1);

        if(used) {
            total_used += count;
        }

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
        assert(b == 'C');
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
                    memset(&small_table[block].entry[i].sha, 0, HASH_SIZE);
                    small_table[block].hash[i] = 0;
                }
            }
            block++;
        }
    } while(block < total_entries);

    size_t s = sizeof(hashblock_t) * total_entries;
    uint64_t crc2 = hash64(&small_table[0], s);
    return crc == crc2 ? 0 : 1;
}

static uint32_t quick(unsigned char init1, unsigned char init2, unsigned char init3, unsigned char init4, const unsigned char *src, size_t len) {
    uint32_t r1 = init1;
    r1 += *reinterpret_cast<const uint8_t *>(src);
    r1 += *reinterpret_cast<const uint8_t *>(src + len - 4);
 
    uint32_t r2 = init2;
    r2 += *reinterpret_cast<const uint8_t *>(src + len / 8 * 2);
    r2 += *reinterpret_cast<const uint8_t *>(src + len / 8 * 3);

    uint32_t r3 = init3;
    r3 += *reinterpret_cast<const uint8_t *>(src + len / 8 * 4);
    r3 += *reinterpret_cast<const uint8_t *>(src + len / 8 * 5);

    uint32_t r4 = init4;
    r4 += *reinterpret_cast<const uint8_t *>(src + len / 8 * 6);
    r4 += *reinterpret_cast<const uint8_t *>(src + len / 8 * 7);

    // No need to mix upper bits because we later modulo an odd value
    return r1 ^ (r2 << 8) ^ (r3 << 16) ^ (r4 << 24);
}

static uint32_t window(const unsigned char *src, size_t len, const unsigned char **pos) {
    size_t i = 0;
    size_t slide = minimum(len / 2, 65536); // slide must be able to fit in hash_t.O. Todo, static assert
    int8_t b = 42;
    uint64_t match = static_cast<size_t>(-1);
#if 0
	for (i = 0; i + 32 < slide; i += 32) {
		__m256i src1 = _mm256_loadu_si256((__m256i*)(&src[i]));
		__m256i src2 = _mm256_loadu_si256((__m256i*)(&src[i + 1]));
		__m256i src3 = _mm256_loadu_si256((__m256i*)(&src[i + slide - 1]));
		__m256i src4 = _mm256_loadu_si256((__m256i*)(&src[i + slide]));
		__m256i sum = _mm256_add_epi8(_mm256_add_epi8(src1, src2), _mm256_add_epi8(src3, src4));
		__m256i comparison = _mm256_cmpeq_epi8(sum, _mm256_set1_epi8(b));
		auto larger = _mm256_movemask_epi8(comparison);
		if (larger != 0) {
#if defined _MSC_VER
			auto off = _tzcnt_u32(static_cast<unsigned>(larger));
#else
			auto off = __builtin_ctz(static_cast<unsigned>(larger));
#endif
			match = i + off;
			break;
		}
	}
#else
    for (i = 0; i + 16 < slide; i += 16) {
        __m128i src1 = _mm_loadu_si128((__m128i *)(&src[i]));
        __m128i src2 = _mm_loadu_si128((__m128i *)(&src[i + 1]));
        __m128i src3 = _mm_loadu_si128((__m128i *)(&src[i + slide - 1]));
        __m128i src4 = _mm_loadu_si128((__m128i *)(&src[i + slide]));
        __m128i sum = _mm_add_epi8(_mm_add_epi8(src1, src2), _mm_add_epi8(src3, src4));
        //sum = _mm_add_epi8(sum, sum);

        __m128i zero_vec = _mm_set1_epi8(b); //  _mm_setzero_si128();
        __m128i comparison = _mm_cmpeq_epi8(sum, zero_vec);

       // __m128i comparison = _mm_cmpeq_epi8(sum, _mm_set1_epi8(0));
        auto larger = _mm_movemask_epi8(comparison);
        if (larger != 0) {
#if defined _MSC_VER
            auto off = _tzcnt_u32(static_cast<unsigned>(larger));
#else
            auto off = __builtin_ctz(static_cast<unsigned>(larger));
#endif
            match = i + off;
            break;
        }
    }
#endif

    if (match == static_cast<uint64_t>(-1)) {
        for (; i < slide; i += 1) {
            uint8_t src1 = src[i];
            uint8_t src2 = src[i + 1];
            uint8_t src3 = src[i + slide - 1];
            uint8_t src4 = src[i + slide];
            uint8_t sum = (src1 + src2) + (src3 + src4);
            //sum = sum + sum;
            signed char h = static_cast<unsigned char>(sum);
            if (h == b) {
                match = i;
                break;
            }
        }
    }
    if (match == static_cast<uint64_t>(-1)) {
        match = slide;
    }

    if (pos != 0) {
        *pos = (unsigned char *)src + match;
    }        

    if(match == slide) {
        return 0;
    }
    else {
        return 1 + quick(src[match], src[match + 16], src[match + len - slide - 32], src[match + len - slide - 4], (unsigned char *)src + match, len - slide - 8);
    }
}

// there must be LARGE_BLOCK more valid data after src + len
const static unsigned char *dub(const unsigned char *src, uint64_t pay, size_t len, size_t block, uint64_t *payload_ref) {
    assert(block == LARGE_BLOCK || block == SMALL_BLOCK);
    const unsigned char *w_pos;
    const unsigned char *orig_src = src;
    const unsigned char *last_src = src + len - 1;
    uint32_t w = window(src, block, &w_pos);
    size_t collision_skip = 32;

    while (src <= last_src) {
        hash_t* e = lookup(w, block == LARGE_BLOCK);

        // CAUTION: Outside mutex, assume reading garbage and that data changes between reads
        if (w != 0 && e) {

            pthread_mutex_lock_wrapper(&table_mutex); // todo, can we relax the mutex scope
            e = lookup(w, block == LARGE_BLOCK);
            if (e && w_pos - e->slide > src && w_pos - e->slide <= last_src) {
                src = w_pos - e->slide;
            }
            pthread_mutex_unlock_wrapper(&table_mutex);

            if (e->offset + block < pay + (src - orig_src)) {
                unsigned char s[HASH_SIZE];

                if (block == LARGE_BLOCK) {
                    unsigned char tmp[8 * 1024];
                    assert(sizeof(tmp) >= LARGE_BLOCK / SMALL_BLOCK * HASH_SIZE);
                    uint32_t k;
                    for (k = 0; k < LARGE_BLOCK / SMALL_BLOCK; k++) {
                        hash(src + k * SMALL_BLOCK, SMALL_BLOCK, g_hash_salt, tmp + k * HASH_SIZE);
                    }
                    hash(tmp, LARGE_BLOCK / SMALL_BLOCK * HASH_SIZE, g_hash_salt, s);
                } else {
                    hash(src, block, g_hash_salt, s);
                }

                pthread_mutex_lock_wrapper(&table_mutex);
                e = lookup(w, block == LARGE_BLOCK);

                if (e && e->offset + block < pay + (src - orig_src) && dd_equal(s, e->sha, HASH_SIZE)) {
                    collision_skip = 8;
                    *payload_ref = e->offset;
                    pthread_mutex_unlock_wrapper(&table_mutex);
                    return src;
                } else {
                    char c;
                    src += collision_skip;
                    collision_skip = collision_skip * 2 > SMALL_BLOCK ? SMALL_BLOCK : collision_skip * 2;
                    c = *src;
                    while (src <= last_src && *src == c) {
                        src++;
                    }
                }

                pthread_mutex_unlock_wrapper(&table_mutex);
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

static bool hashat(const unsigned char *src, uint64_t pay, size_t len, bool large, unsigned char *hash, int overwrite) {
    const unsigned char *o;
    uint32_t w = window(src, len, &o);
    if(w != 0) {
        pthread_mutex_lock_wrapper(&table_mutex);
        hash_t e;            

        //e.hash = w;
        e.offset = pay;
        memcpy((unsigned char *)e.sha, hash, HASH_SIZE);
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

static size_t write_match(size_t length, uint64_t payload, unsigned char *dst) {
   // wcerr << L"match = " << length << L"," << payload << L"\n";

    if (length > 0) {
        if(length == LARGE_BLOCK) {
            largehits += length;
        }
        else {
            smallhits += length;
        }

        memcpy(dst, DUP_MATCH, 2);
        dst += 8 - 6;
        ll2str(32 - (6 + 8), (char *)dst, 4);
        dst += 4;
        ll2str(length, (char *)dst, 4);
        dst += 4;
        ll2str(payload, (char *)dst, 8); 
        dst += 8;
        return 32 - (6 + 8);
    }
    return 0;
}

static size_t write_literals(const unsigned char *src, size_t length, unsigned char *dst, int thread_id, bool entropy) {
  //  wcerr << L"literal = " << length << L" ";


    if (length > 0) {
        size_t r;
        if (level == 0 || entropy) {
            dst[32 - (6 + 8)] = '0';
            memcpy(dst + 33 - (6 + 8), src, length);
            r = length + 1;
        } else if (level >= 1 && level <= 3) {
            int zstd_level = level == 1 ? 1 : level == 2 ? 10 : 19;
            dst[32 - (6 + 8)] = char(level + '0');
            r = zstd_compress((char *)src, length, (char *)dst + 33 - (6 + 8) + 4 + 4, 2 * length + 1000000, zstd_level, jobs[thread_id].zstd);
            *((int32_t *)(dst + 33 - (6 + 8))) = (int32_t)r;
            r += 4; // LEN C
            *((int32_t *)(dst + 33 - (6 + 8) + 4)) = (int32_t)length;
            r += 4; // LEN D
            r++;    // The '1'
        } else {
            // todo, handle outside lib
            fprintf(stderr, "\neXdupe: Internal error, bad compression level\n");
            exit(-1);
        }

        memcpy(dst, DUP_LITERAL, 2);
        dst += 8 - 6;
        ll2str(r + 32 - (6 + 8), (char *)dst, 4);
        dst += 4;
        ll2str(length, (char *)dst, 4);
        dst += 4;
        ll2str(0, (char *)dst, 8);
        dst += 8;

        stored_as_literals += length;
        literals_compressed_size += r + 32 - (6 + 8);

        return r + 32 - (6 + 8);
    }
    return 0;
}

static void hash_chunk(const unsigned char *src, uint64_t pay, size_t length, int policy) {
    char tmp[512 * HASH_SIZE];
    assert(sizeof(tmp) >= HASH_SIZE * LARGE_BLOCK / SMALL_BLOCK);

    size_t small_blocks = length / SMALL_BLOCK;
    uint32_t smalls = 0;
    uint32_t j = 0;

    for (j = 0; j < small_blocks; j++) {
        hash(src + j * SMALL_BLOCK, SMALL_BLOCK, g_hash_salt, (unsigned char *)tmp + smalls * HASH_SIZE);
        bool success_small = hashat(src + j * SMALL_BLOCK, pay + j * SMALL_BLOCK, SMALL_BLOCK, false, (unsigned char *)tmp + smalls * HASH_SIZE, policy);
        if(!success_small) {
            anomalies_small += SMALL_BLOCK;
        }

        smalls++;
        if (smalls == LARGE_BLOCK / SMALL_BLOCK) {
            unsigned char tmp2[HASH_SIZE];
            hash((unsigned char *)tmp, smalls * HASH_SIZE, g_hash_salt, tmp2);
            bool success_large = hashat(src + (j + 1) * SMALL_BLOCK - LARGE_BLOCK, pay + (j + 1) * SMALL_BLOCK - LARGE_BLOCK, LARGE_BLOCK, true, (unsigned char *)tmp2, policy);
            if(!success_large) {
                anomalies_large += SMALL_BLOCK;
            }

            smalls = 0;
        }
    }

#ifdef HASH_PARTIAL_BLOCKS
    size_t large_blocks = length / LARGE_BLOCK;
    size_t rem_size = length - SMALL_BLOCK * small_blocks;
    size_t rem_offset = SMALL_BLOCK * small_blocks;
    if (rem_size >= 128) {
        sha(src + rem_offset, rem_size, (unsigned char *)tmp);
        hashat(src + rem_offset, pay + rem_offset, rem_size, 0, (unsigned char *)tmp, policy);
    }

    // Hash any remainder smaller than LARGE_BLOCK
    rem_size = length - LARGE_BLOCK * large_blocks;
    rem_offset = LARGE_BLOCK * large_blocks;
    if (rem_size >= SMALL_BLOCK) {
        sha(src + rem_offset, rem_size, (unsigned char *)tmp);
        hashat(src + rem_offset, pay + rem_offset, rem_size, 1, (unsigned char *)tmp, policy);
    }
#endif
    return;
}


static size_t process_chunk(const unsigned char* src, uint64_t pay, size_t length, unsigned char* dst, int thread_id) {
    const unsigned char* upto;
    const unsigned char* src_orig = src;
    unsigned char* dst_orig = dst;
    const unsigned char* last = src + length - 1;

    while (src <= last) {
        uint64_t ref = 0;
        const unsigned char* match = 0;

        if (src + LARGE_BLOCK - 1 <= last) {
            match = dub(src, pay + (src - src_orig), last - src, LARGE_BLOCK, &ref);
        }
        upto = (match == 0 ? last : match - 1);

        while (src <= upto) {
            uint64_t ref_s = 0;
            const unsigned char* match_s = 0;
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
                auto min = minimum(n * SMALL_BLOCK, upto - match_s + 1);
                dst += write_match(min, ref_s, dst);
                src = match_s + min;
            }
        }

        if (match == 0) {
            return dst - dst_orig;
        }
        else {
            dst += write_match(minimum(LARGE_BLOCK, last - match + 1), ref, dst);
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

        int policy = 1;


       // me->size_destination = process_chunk(me->source, me->payload, me->size_source, me->destination, me->id);
       // hash_chunk(me->source, me->payload, me->size_source, policy);

        if(!me->entropy) {
            hash_chunk(me->source, me->payload, me->size_source, policy);
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

#ifdef WINDOWS
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

size_t dup_size_compressed(const unsigned char *src) {
    size_t t = str2ll(src + 8 - 6, 4);
    return t;
}

size_t dup_size_decompressed(const unsigned char *src) {
    size_t t = str2ll(src + 16 - 4 - 6, 4);
    return t;
}

uint64_t dup_counter_payload(void) { return count_payload; }

void dup_counters_reset(void) {
    count_payload = 0;
}

static uint64_t packet_payload(const unsigned char *src) {
    uint64_t t = str2ll(src + 24 - (6 + 8), 8);
    return t;
}

int dup_decompress(const unsigned char *src, unsigned char *dst, size_t *length, uint64_t *payload) {
    if (zstd_decompress_state == 0) {
        zstd_decompress_state = zstd_init();
    }

    if (dd_equal(src, DUP_LITERAL, 2)) {
        size_t t;
        src += 32 - (6 + 8);

        if (*src == '0') {
            t = dup_size_decompressed(src - 32 + (6 + 8));
            memcpy(dst, src + 1, t);
        } else if (*src == '1' || *src == '2' || *src == '3') {
            int32_t len = *(int32_t *)((src) + 1);
            int32_t len_de = *(int32_t *)((src) + 1 + 4);
            t = zstd_decompress((char *)(src) + 1 + 4 + 4, len, (char *)dst, len_de, 0, 0, zstd_decompress_state);
            t = len_de;
        } else {
            // todo, handle outside lib
            fprintf(stderr, "\neXdupe: Internal error or archive corrupted, "
                            "missing compression level block header");
            exit(-1);
        }

        *length = t;
        count_payload += *length;
        return 0;
    }
    if (dd_equal(src, DUP_MATCH, 2)) {
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

// todo rename
int dup_decompress_simulate(const unsigned char *src, size_t *length, uint64_t *payload) {
    if (dd_equal(src, DUP_LITERAL, 2)) {
        size_t t;

        src += 2;
        src += 4;
        t = str2ll(src, 4);

        *length = t;
        if (t == 0) {
            return -1;
        }

        return 0;
    }
    if (dd_equal(src, DUP_MATCH, 2)) {
        uint64_t pay = packet_payload(src);
        size_t len = dup_size_decompressed(src);
        *payload = pay;
        *length = len;
        return 1;
    } else {
        return -2;
    }
}

size_t flush_pend(char *dst, uint64_t *payloadret) {
    char *orig_dst = dst;
    *payloadret = 0;
    int i;
    for (i = 0; i < threads; i++) {
        pthread_mutex_lock_wrapper(&jobs[i].jobmutex);
        if (!jobs[i].busy && jobs[i].size_destination > 0 && jobs[i].payload == flushed) {
            memcpy(dst, jobs[i].destination, jobs[i].size_destination);
            dst += jobs[i].size_destination;
            flushed += jobs[i].size_source;
            jobs[i].size_destination = 0;
            *payloadret = jobs[i].size_source;
            jobs[i].size_source = 0;
            pthread_mutex_unlock_wrapper(&jobs[i].jobmutex);
            break;
        }
        pthread_mutex_unlock_wrapper(&jobs[i].jobmutex);
    }
    return dst - orig_dst;
}

uint64_t dup_get_flushed() { return flushed; }

size_t dup_compress(const void *src, char *dst, size_t size, uint64_t *payload_returned, bool entropy) {
    char *dst_orig = dst;
    *payload_returned = 0;

    if(entropy) {
        high_entropy += size;
    }


#if 0 // single threaded naive for debugging
	if (size > 0)
	{
		size_t t = process_chunk((unsigned char*)src, global_payload, size, (unsigned char*)dst, 1);
		if (add_data) {
			hash_chunk((unsigned char*)src, global_payload, size);
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
            dst += flush_pend(dst, payload_returned);
            f = get_free();

            assert(!(dst != dst_orig && f == -1));

            if (f == -1) {
                pthread_cond_wait_wrapper(&jobdone_cond, &jobdone_mutex);
            }
        } while (f == -1);

        size_t req = size + 3 * LARGE_BLOCK;
        if(jobs[f].source_capacity < req) {
            // todo, use vector
            free(jobs[f].source);
            free(jobs[f].destination);
            jobs[f].source = (unsigned char*)malloc(size_t(1.5 * req));
            jobs[f].destination = (unsigned char*)malloc(size_t(1.5 * req));
            jobs[f].source_capacity = size_t(1.5 * req);
        }
        memcpy(jobs[f].source, src, size);
        jobs[f].payload = global_payload;
        global_payload += size;
        count_payload += size;
        jobs[f].size_source = size;
        jobs[f].entropy = entropy;

        pthread_cond_signal_wrapper(&jobs[f].cond);
        pthread_mutex_unlock_wrapper(&jobs[f].jobmutex);
        pthread_mutex_unlock_wrapper(&jobdone_mutex);
    }

    return dst - dst_orig;
}
