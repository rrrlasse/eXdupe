

#ifndef DEDUPE_HEADER
#define DEDUPE_HEADER

#define DUP_BLOCK 'D'

#include <stdint.h>
#include <string.h>

uint64_t dup_memory(uint64_t bits);
int dup_init(size_t large_block, size_t small_block, uint64_t memory_usage,
	     int max_threadcount, void *memory, int compression_level,
	     bool crypto_hash, uint64_t hash_seed);

size_t dup_compress(const void *src, char *dst, size_t size,
		    uint64_t *payloadreturned);
int dup_decompress(const unsigned char *src, unsigned char *dst, size_t *length,
		   uint64_t *payload);
int dup_decompress_simulate(const unsigned char *src, size_t *length,
			    uint64_t *payload);

size_t dup_size_compressed(const unsigned char *src);
size_t dup_size_decompressed(const unsigned char *src);

void dup_counters_reset(void);
uint64_t dup_counter_payload(void);
uint64_t dup_counter_compressed(void);

void dup_add(bool add);
size_t dup_compress_hashtable(void);
int dup_decompress_hashtable(size_t len);
void dup_deinit(void);

void reset_profiling(void);
void print_profiling(void);
uint64_t dup_get_flushed(void);
size_t flush_pend(char *dst, uint64_t *payloadreturned);

uint64_t larges();
uint64_t smalls();

#endif
