#pragma once

#include <stdint.h>
#include <string.h>
#include <atomic>

#ifdef _WIN32
#include "pthread/pthread.h" // todo, move to .cpp
#include <Windows.h>
#include <intrin.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

/*
Compressed data format: A sequence of packets. A packet can either be a reference 
(header starts with "R") or a literal (header starts with "L"):

R cccc dddd pppppppp
  c: 32-bit, size of this packet, i.e the value 17 (17 bytes)
  d: 32-bit, number of bytes to copy from p
  p: 64-bit pointer into past user payload (p + d can not point beyond current position)

L cccc dddd pppppppp <data compressed with some traditional data compression>
 c = 32-bit, size of this packet, including data (i.e. 17 + data)
 d = Size in bytes of the *decompressed* data that follows this header
 p = Offset into the user payload that this package represents
*/

#define DUP_REFERENCE 'R'
#define DUP_LITERAL 'L'
#define DUP_HEADER_LEN 17

int dup_init_compression(size_t, size_t, uint64_t, int, void *, int, bool, uint64_t, uint64_t);
void dup_uninit_compression(void);
void dup_init_decompression();
void dup_uninit_decompression();

size_t dup_compress(const void *, char *, size_t, uint64_t *, bool, char**, uint64_t*);
int dup_decompress(const char *, char *, size_t *, uint64_t *);
int dup_packet_info(const char *, size_t *,
			    uint64_t *);

size_t dup_size_compressed(const char *);
size_t dup_size_decompressed(const char *);

uint64_t dup_counter_payload(void);
size_t dup_compress_hashtable(char*);
int dup_decompress_hashtable(char*);


void print_fillratio(void);
uint64_t dup_get_flushed(void);
size_t dup_flush_pend(uint64_t*, char**, uint64_t*);
size_t dup_flush_pend_block(uint64_t*, char**, uint64_t*);
void print_table();

// When compressing the hashtable, worst case is that all entries are in use, in which case it ends up
// growing COMPRESSED_HASHTABLE_OVERHEAD bytes in size.
#define COMPRESSED_HASHTABLE_OVERHEAD 4096
#define HASH_SIZE 16
#define SLOTS 8

#pragma pack(push, 1)
struct hash_t {
    uint64_t offset;
    uint16_t slide;
    char sha[HASH_SIZE];
    uint8_t first_byte;
};

struct hashblock_t {
    uint32_t hash[SLOTS];
    hash_t entry[SLOTS];
};
#pragma pack(pop)

// todo, move to .cpp (make char* jobs) so zstd can be typed
// todo, move pthread to cpp
struct job_t {
    pthread_t thread;
    char* source = 0;
    char* destination = 0;
    uint64_t payload = 0;
    size_t size_source = 0;
    size_t size_destination = 0;
    pthread_mutex_t jobmutex;
    pthread_cond_t cond;
    int id = -1;
    char* zstd = 0;
    bool busy = false;
    bool entropy = false;
    bool cancel = false;
};

// todo, clean up all the counters like "payload, flushed, global_payload, count_payload",
// some are redundant, others need renaming
struct state_compress_t {
    bool g_crypto_hash = false;
    uint64_t g_hash_salt = 0;
    pthread_mutex_t table_mutex;
    pthread_cond_t jobdone_cond;
    pthread_mutex_t jobdone_mutex;

    int threads = -1;
    int level = -1;

    size_t SMALL_BLOCK = -1;
    size_t LARGE_BLOCK = -1;
    const uint64_t size_ratio = 32;
    uint64_t small_entries = -1;
    uint64_t large_entries = 0;
    hashblock_t* small_table = 0;
    hashblock_t* large_table = 0;

    char* memory_begin = 0;
    char* memory_table = 0;
    char* memory_end = 0;
    size_t memsize = 0;

    uint64_t flushed = 0;
    uint64_t global_payload = 0;
    uint64_t count_payload = 0;
    
    job_t* jobs = 0;

    // todo, C
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
};

// todo, global
extern state_compress_t state_c;
