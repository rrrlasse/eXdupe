#include <immintrin.h> // For AVX2
#include <intrin.h>    // For _tzcnt_u32

#include <iostream>

/*
eXdupe Archiver, copyright 2010 - 2012 by eXdupe.com. All rights reserved.

eXdupe is traditional proprietary software, with parts of the source code being available under 
restricted non-permissive terms:

You may modify and compile eXdupe, and we encourage you to submit bugfixes or new features to 
us. However, redistribution of original or modified source code or binaries, or any derived 
work, is probitted.

EXDUPE IS NOT FREE. Use of original or modified eXdupe or any derived work requires you to
purchase a license (see http://www.exdupe.com/). 

eXdupe contains 3'rd party source code files that carry their own original preamble terms and are not 
covered by above terms.
*/

#define compile_assert(x) int __dummy[(int)x]; static_cast<void>(__dummy);

#include "libexdupe.h"
#include "bzip2/bzlib.h"
#include "zlib/zlib.h"

#ifndef INLINE
	#define INLINE
#endif

#define DUP_MAX_INPUT (32*1024*1024)
#define DUP_MATCH "DM"
#define DUP_LITERAL "DL"
 
#if defined(__SVR4) && defined(__sun)
	#include <thread.h>
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64)
	#define WINDOWS
#endif

#if (defined(__X86__) || defined(__i386__) || defined(i386) || defined(_M_IX86) || defined(__386__) || defined(__x86_64__) || defined(_M_X64))
	#define X86X64
#endif

#include <stdio.h>
#include <assert.h>
#include "quicklz/quicklz.h"
#include "skein/skein.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef WINDOWS
	#include <Windows.h>
#define HAVE_STRUCT_TIMESPEC
	#include "pthread/pthread.h"
#define SLEEP1SEC Sleep(1000);
#else
	#define SLEEP1SEC sleep(1);
	#include <unistd.h>
	#include <pthread.h>
#endif

//#define THREADTEST

#ifdef THREADTEST

uint64_t lr(void)
{
	return ((uint64_t)rand()) | (((uint64_t)rand()) << 12) | (((uint64_t)rand()) << 24);
}

void RANDSLEEP(void)
{
	if((rand() % 10000) == 1)
	{
		Sleep(rand() % 1000);
	}
	else if((rand() % 1000) == 1)
	{
		Sleep((rand() % 100) == 1);
	}
	else if((rand() % 100) == 1)
	{
		Sleep(rand() % 10);
	}
}

int pthread_mutex_trylock2(pthread_mutex_t * mutex)
{
	RANDSLEEP();
	int i = pthread_mutex_trylock(mutex);
	RANDSLEEP();
	return i;
}

#define SURROUND(arg) \
	RANDSLEEP(); \
	arg; \
	RANDSLEEP();

#define pthread_mutex_lock(mutex) SURROUND(pthread_mutex_lock(mutex))
#define pthread_mutex_unlock(mutex) SURROUND(pthread_mutex_unlock(mutex))
#define pthread_cond_wait(mutex, cond) SURROUND(pthread_cond_wait(mutex, cond))
#define pthread_cond_broadcast(cond) SURROUND(pthread_cond_broadcast(cond))
#define pthread_cond_signal(cond) SURROUND(pthread_cond_signal(cond))

#define pthread_mutex_trylock pthread_mutex_trylock2
#endif


size_t SMALL_BLOCK;
size_t LARGE_BLOCK;

uint64_t SMALL_PRIME = 0x123456783aad8471ULL;
#define SHA_SIZE 11 // 256 bits

#define MEGA (1024*1024)
#define KILO 1024

int THREADS = 4;
int LEVEL = 1;	// 1 or 2

bool exit_threads;

pthread_mutex_t table_mutex;
pthread_mutex_t jobdone_mutex;
pthread_cond_t jobdone_cond;

uint64_t HASH_ENTRIES;

bool add_data = true;

size_t tbl_size;

#pragma pack(push, 1)
typedef struct
{
	uint64_t offset;
	uint32_t hash;
	unsigned char sha[SHA_SIZE];
	unsigned char copy[5];       
} hash_t;
#pragma pack(pop)

static bool used(hash_t& h) {
	return (h.offset != 0 && h.hash != 0);
}

hash_t (*table)[2];

qlz_state_decompress state_decompress;
bz_stream bzip2d;
z_stream zlibd;

uint64_t largehits = 0;
uint64_t smallhits = 0;

template <class T, class U> const uint64_t minimum (const T a, const U b) {
  return (static_cast<uint64_t>(a) > static_cast<uint64_t>(b)) ? static_cast<uint64_t>(b) : static_cast<uint64_t>(a);  
}

INLINE static void utils_yield(void)
{
// don't use sleep(0) because it yields multiple time slices
#ifdef WINDOWS
    sched_yield();
#elif defined(__SVR4) && defined(__sun)
    thr_yield(); // Solaris
#else
    sched_yield();  // other *nix
#endif
}


INLINE static bool dd_equal(const void *src1, const void *src2, size_t len)
{
	size_t i;
	char *s1 = (char *)src1;
	char *s2 = (char *)src2;
	for(i = 0; i < len; i++)
		if(s1[i] != s2[i])
			return false;
	return true;
}


INLINE static void ll2str(uint64_t l, char *dst, int bytes)
{
	while(bytes > 0)
	{
		*dst = (l & 0xff);
		dst++;
		l = l >> 8;
		bytes--;
	}
}

INLINE static uint64_t str2ll(const void *src, int bytes)
{
	unsigned char *src2 = (unsigned char *)src;
	uint64_t l = 0;
	while(bytes > 0)
	{
		bytes--;
		l = l << 8;
		l = (l | *(src2 + bytes));
	}
	return l;
}


typedef struct
{
	pthread_cond_t cond;
	pthread_t thread;
	int status;
	unsigned char source[DUP_MAX_INPUT];
	unsigned char destination[DUP_MAX_INPUT + 1024*1024];
	uint64_t payload;
	size_t size_source;
	size_t size_destination;
	pthread_mutex_t mutex;
	int id;
	qlz_state_compress qlz;
	bz_stream bzip2c;
	z_stream zlibc;
	bool add;
	bool busy;
} job_t;

job_t *jobs;



INLINE static void sha(const unsigned char *src, size_t len, unsigned char *dst)
{
	unsigned char d[100];
	Skein_256_Ctxt_t ctx; 
	Skein_256_Init(&ctx, 256);
	Skein_256_Update(&ctx, src, len);
	Skein_256_Final(&ctx, d);
	memcpy(dst, d, SHA_SIZE);
}

INLINE static uint64_t shall(const void *src, size_t len) 
{
	char *src2 = (char *)src;
	uint64_t l = 0;
	uint64_t a_val = 0xd20f9a8b761b7e4cULL;
	uint64_t b_val = 0x994e80091d2f0bc3ULL;

	while(len >= 8)
	{
#ifdef X86X64
		a_val += (*(uint64_t *)src2) * b_val;
#else		
		for(uint32_t i = 0; i < 8; i++)
		{
			l = l >> 8;
			l = l | (uint64_t)*(src2 + i) << (7*8);
		}
		a_val += l * b_val;
#endif
		b_val += a_val;
		len -= 8;
		src2 += 8;
	}

	while(len > 0)
	{
		l = l >> 8;
		l = l | (uint64_t)*src2 << (7*8);
		src2++;
		len--;
	}
	a_val += l * b_val;
	b_val += a_val;
	return a_val + b_val;
}

size_t dup_table_condense(void)
{
	uint64_t hash;
	char *dst = (char *)table;
	size_t i, j;
	size_t null_entries = 0;
	size_t siz;

	hash = shall(table, 2*sizeof(hash_t)*HASH_ENTRIES);

	for(i = 0; i < HASH_ENTRIES; i++)
	{
		for(j = 0; j < 2; j++)
		{
			if (used(table[i][j]) || (j == 1 && i == HASH_ENTRIES - 1))
			{
				if(j == 1 && i == HASH_ENTRIES - 1 && !used(table[i][j]))
					null_entries++;

				if(null_entries > 0)
				{
					ll2str(null_entries, dst, 8);
					dst += 8;
					*dst++ = 0x11;
				}
				
				if(used(table[i][j]))
				{
					hash_t h;
					memcpy(&h, &table[i][j], sizeof(hash_t));

					ll2str(h.offset, dst, 8);
					ll2str(h.hash, dst + 8, 4);
					memcpy(dst + 8 + 4, h.copy, 5);
					memcpy(dst + 8 + 4 + 5, h.sha, SHA_SIZE);

					dst += 8 + 4 + 5 + SHA_SIZE;
					*dst++ = 0x22;
				}
				null_entries = 0;
			}			
			else
			{
				null_entries++;
			}
		}
	}

	siz = dst - (char *)table;
	ll2str(hash, (char *)table + siz, 8);
	siz += 8;

	return siz;	
}

int dup_table_expand(size_t len)
{
	unsigned char *src = (unsigned char *)table + len - 1;
	size_t null_entries = 0;
	int64_t i, j;
	uint64_t hash = str2ll(src - 7, 8);
	src -= 8;

	for(i = HASH_ENTRIES - 1; i >= 0; i--)
	{
		for(j = 1; j >= 0; j--)
		{
			if(null_entries == 0)
			{
				if (*src == 0x11)
				{
					src -= 8;
					null_entries = str2ll(src, 8);
					src--;

					null_entries--;
					memset(&table[i][j], 0, sizeof(hash_t));
				}
				else if (*src == 0x22)
				{
					unsigned char temp[1000];
					src -= (8 + 4 + 5 + SHA_SIZE);

					memcpy(temp, src, 8 + 4 + 5 + SHA_SIZE);
					memset(&table[i][j], 0, sizeof(hash_t));

					table[i][j].offset = str2ll(temp, 8);
					table[i][j].hash = static_cast<uint32_t>(str2ll(temp + 8, 4));
					memcpy(table[i][j].copy, temp + 8 + 4, 5);
					memcpy(table[i][j].sha, temp + 8 + 4 + 5, SHA_SIZE);
					
					//table[i][j].used = 1;
					src--;
				}
				else
				{
					fprintf(stderr, "\neXdupe: Internal error or archive corrupted, at table_expand()\n");
					exit(-1);
				}
			}
			else
			{
				null_entries--;
				memset(&table[i][j], 0, sizeof(hash_t));
			}
		}
	}

	if (hash != shall(table, 2*sizeof(hash_t)*HASH_ENTRIES))
	{
		fprintf(stderr, "\neXdupe: Internal error or archive corrupted, at table_expand(), at hashtable\n");
		return -1;
	}
	return 0;
}



INLINE static uint64_t entry(uint64_t window)
{
	return window % HASH_ENTRIES;
}


INLINE static uint32_t quick(const unsigned char *src, size_t len) // maybe not needed
{
	uint64_t res = 0;
	size_t i;
	for(i = 0; i < len - len/10 - 10; i += len/10)
	{
		uint64_t l;
#ifdef X86X64
		l = *reinterpret_cast<const uint64_t *>(src + i);
#else
		l = str2ll(src + i, 8);
#endif
		res = res ^ ((l + 1)*i*SMALL_PRIME);
	}
	return static_cast<uint32_t>(res ^ (res >> 32));
}


INLINE static uint32_t window(const unsigned char *src2, size_t len, const unsigned char **pos)
{
	int8_t * src = (int8_t*)src2;

	uint64_t i = 0;
	size_t slide = len / 8;
	size_t percent = (len - slide) / 100;
		
	int8_t b = len >= 8 * 1024 ? 1 : (6 * 1024) / len;
	int position = -1;
	int position_verify = -1;

	b = -128 + b;


	// 256k must give 1
	// 1k must give 8 (4 hits per 1 kb on AVERAGE)
	// 2 =          4
	// 4 =			2
	// 8 =          1






#if 0
	for (i = 0; i + 32 < slide; i += 32) {
		__m256i src1 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(&src[i]));
		__m256i src2 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(&src[i + 20 * percent]));
		__m256i src3 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(&src[i + 80 * percent]));
		__m256i src4 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(&src[i + len - slide - 4]));
		__m256i sum = _mm256_add_epi8(_mm256_add_epi8(src1, src2), _mm256_add_epi8(src3, src4));
		__m256i comparison = _mm256_cmpgt_epi8(sum, _mm256_set1_epi8(b - 1));
		auto larger = _mm256_movemask_epi8(comparison);
		if (larger != -1) {
			auto off = _tzcnt_u32(static_cast<unsigned>(~larger));
			position = i + off;
			break;
		}
	}
	if (position == -1) {
		for (; i < slide; i += 1) {
			signed char h = static_cast<unsigned char>(src[i] + src[i + 20 * percent] + src[i + 80 * percent] + src[i + len - slide - 4]);
			if (h < b) {
				position = i;
				break;
			}
		}
	}

	

#else
	
	

	for (i = 0; i + 16 < slide; i += 16) {
		__m128i src1 = _mm_loadu_si128(reinterpret_cast<__m128i*>(&src[i]));
		__m128i src2 = _mm_loadu_si128(reinterpret_cast<__m128i*>(&src[i + 20 * percent]));
		__m128i src3 = _mm_loadu_si128(reinterpret_cast<__m128i*>(&src[i + 80 * percent]));
		__m128i src4 = _mm_loadu_si128(reinterpret_cast<__m128i*>(&src[i + len - slide - 4]));
		__m128i sum = _mm_add_epi8(_mm_add_epi8(src1, src2), _mm_add_epi8(src3, src4));
		__m128i comparison = _mm_cmpgt_epi8(sum, _mm_set1_epi8(b - 1));
		auto larger = _mm_movemask_epi8(comparison);
		if (larger != 0xffff) {
			auto off = _tzcnt_u32(static_cast<unsigned>(~larger));
			position = i + off;
			break;
		}
	}
	if (position == -1) {
		for (; i < slide; i += 1) {
			signed char h = static_cast<unsigned char>(src[i] + src[i + 20 * percent] + src[i + 80 * percent] + src[i + len - slide - 4]);
			if (h < b) {
				position = i;
				break;
			}
		}
	}


#endif












	if (position == -1) {
		position = slide;
	}


	if(pos != 0)
		*pos = (unsigned char*)src + position;


	return quick((unsigned char*)src + position, len - slide - 8);
}

// there must be LARGE_BLOCK more valid data after src + len
INLINE const static unsigned char *dub(const unsigned char *src, uint64_t pay, size_t len, size_t block, int no, uint64_t *payload_ref)
{	
	const unsigned char *w_pos;
	const unsigned char *orig_src = src;
	const unsigned char *last_src = src + len - 1;
	uint64_t w;
	w = window(src, block, &w_pos);
	size_t collision_skip = 32;

	while(src <= last_src)
	{
		uint64_t j = entry(w);

		if (table[j][no].hash == w)
		{
			#define CONDITION table[j][no].hash == w && src[0] == table[j][no].copy[0] && src[block - 1] == table[j][no].copy[4] && src[block / 4 * 1] == table[j][no].copy[1] && src[block / 4 * 2] == table[j][no].copy[2] && src[block / 4 * 3] == table[j][no].copy[3] && used(table[j][no]) && (   (!add_data) || (table[j][no].offset + block < pay + (src - orig_src)))

			if(CONDITION) // we need check here and one in a mutex later
			{
				unsigned char s[SHA_SIZE];

				if(block == LARGE_BLOCK)
				{
					uint32_t k;
					unsigned char tmp[8*KILO]; // fixme
					for(k = 0; k < LARGE_BLOCK / SMALL_BLOCK; k++)
						sha(src + k * SMALL_BLOCK, SMALL_BLOCK, tmp + k * SHA_SIZE);
					sha(tmp, LARGE_BLOCK / SMALL_BLOCK * SHA_SIZE, s);
				}
				else
				{
					sha(src, block, s);
				}

				pthread_mutex_lock(&table_mutex);

				if(dd_equal(s, table[j][no].sha, SHA_SIZE) && CONDITION)
				{
					collision_skip = 32;
					*payload_ref = table[j][no].offset;
					pthread_mutex_unlock(&table_mutex);

					if(block == LARGE_BLOCK)
						largehits++;
					else
						smallhits++;

					return src;
				}
				else
				{
					char c;
					src += collision_skip;
					collision_skip = collision_skip * 2 > LARGE_BLOCK ? LARGE_BLOCK : collision_skip * 2;
					c = *src;
					while(src <= last_src && *src == c)
						src++;
				}

				pthread_mutex_unlock(&table_mutex);
			}
		}
		else
			src = w_pos;

		src++;

		if (w_pos < src)
		{
			w = window(src, block, &w_pos);
		}
	}

	return 0;			
}


INLINE static void hashat(const unsigned char *src, uint64_t pay, size_t len, int no, unsigned char *hash, int overwrite)
{
	uint64_t w = window(src, len, 0);
	uint64_t j = entry(w);
	
	pthread_mutex_lock(&table_mutex);

	if((overwrite == 0 && !used(table[j][no])) ||
	   (overwrite == 1 && (!used(table[j][no]) || table[j][no].hash != w)) ||
	   (overwrite == 2))
	{
//		table[j][no].used = 1;
		table[j][no].hash = static_cast<uint32_t>(w);
		table[j][no].offset = pay;

		memcpy((unsigned char *)table[j][no].sha, hash, SHA_SIZE);

		table[j][no].copy[0] = src[0];
		table[j][no].copy[1] = src[len / 4 * 1];
		table[j][no].copy[2] = src[len / 4 * 2];
		table[j][no].copy[3] = src[len / 4 * 3];
		table[j][no].copy[4] = src[len - 1];
	}

	pthread_mutex_unlock(&table_mutex);

}

INLINE static size_t write_match(size_t length, uint64_t payload, unsigned char *dst)
{
	if(length > 0)
	{
		memcpy(dst, DUP_MATCH, 2);
		dst += 8 - 6;
		ll2str(32 - (6 + 8), (char *)dst, 4);
		dst += 4;
		ll2str(length, (char *)dst, 4);
		dst += 4;
		ll2str(payload, (char *)dst, 8);
		dst += 8;
		return 32 - (6+8);
	}
	return 0;
}

INLINE static size_t write_literals(const unsigned char *src, size_t length, unsigned char *dst, int thread_id)
{
	if(length > 0)
	{
		size_t r;
		if(LEVEL == 0) 
		{
			dst[32 - (6+8)] = '0';
			memcpy(dst + 33 - (6+8), src, length);
			r = length + 1;
		}	
		else if(LEVEL == 1) 
		{
			dst[32 - (6+8)] = '1';
			r = qlz_compress(src, (char *)dst + 33 - (6+8), length, &jobs[thread_id].qlz);
			r++;
		}
		else if(LEVEL == 2)
		{
			dst[32 - (6+8)] = '2';
			jobs[thread_id].zlibc.zalloc = 0;
			jobs[thread_id].zlibc.zfree = 0;
			int rv  = deflateInit(&jobs[thread_id].zlibc, 1);
			if(rv != Z_OK) {
				fprintf(stderr, "\neXdupe: Error at deflateInit(). System out of memory.\n");
				exit(-1);
			}

			jobs[thread_id].zlibc.avail_in = static_cast<uInt>(length);
			jobs[thread_id].zlibc.avail_out = DUP_MAX_INPUT + 1024*1024;
			jobs[thread_id].zlibc.next_in = (Bytef *)src;
			jobs[thread_id].zlibc.next_out = (Bytef *)dst + 33 - (6+8);

			rv = deflate(&jobs[thread_id].zlibc, Z_FINISH);
			if(rv != Z_STREAM_END) {
				fprintf(stderr, "\neXdupe: Internal error at deflate()\n");
				exit(-1);
			}


			deflateEnd(&jobs[thread_id].zlibc);
			r = (char *)jobs[thread_id].zlibc.next_out - (char *)dst + 33 - (6+8);
			r += 1; // the '2'
		}
		else if(LEVEL == 3) 
		{
			dst[32 - (6+8)] = '3';
			jobs[thread_id].bzip2c.bzalloc = 0;
			jobs[thread_id].bzip2c.bzfree = 0;
			jobs[thread_id].bzip2c.opaque = reinterpret_cast<void*>(thread_id);

			int rv = BZ2_bzCompressInit(&jobs[thread_id].bzip2c, 3, 0, 0);
			if(rv != BZ_OK) {
				fprintf(stderr, "\neXdupe: Error at BZ2_bzCompressInit(). System out of memory.\n");
				exit(-1);
			}
			jobs[thread_id].bzip2c.avail_in = static_cast<uInt>(length);
			jobs[thread_id].bzip2c.avail_out = DUP_MAX_INPUT + 1024*1024;
			jobs[thread_id].bzip2c.next_in = (char *)src;
			jobs[thread_id].bzip2c.next_out = (char *)dst + 33 - (6+8);

			int res = BZ2_bzCompress(&jobs[thread_id].bzip2c, BZ_FINISH);
			if(res != BZ_STREAM_END) {
				fprintf(stderr, "\neXdupe: Internal error at BZ2_bzCompress()\n");
				exit(-1);
			}

			BZ2_bzCompressEnd(&jobs[thread_id].bzip2c);

			r = jobs[thread_id].bzip2c.next_out - (char *)dst + 33 - (6+8);
			r += 1; // the '2'
		}
		else
		{
			fprintf(stderr, "\neXdupe: Internal error, bad compression level\n");
			exit(-1);
		}

		memcpy(dst, DUP_LITERAL, 8 - 6);
		dst += 8 - 6;
		ll2str(r + 32 - (6+8), (char *)dst, 4);
		dst += 4;
		ll2str(length, (char *)dst, 4);
		dst += 4;
		ll2str(0, (char *)dst, 8);
		dst += 8;
		return r + 32 - (6 + 8);
	}
	return 0;
}


//#define NAIVE

static size_t cons_flush(unsigned char *dst, uint64_t *q_pay, uint64_t *q_len, uint64_t *q_com)
{
#ifdef NAIVE
	return 0;
#endif

	unsigned char *orig_dst = dst;
	if(*q_len > 0)
	{
		dst += write_match(*q_len, *q_pay, dst);
		*q_com += *q_len;
		*q_len = 0;
	}
	return dst - orig_dst;
}

static size_t cons_match(size_t length, uint64_t payload, unsigned char *dst, uint64_t *q_pay, uint64_t *q_len, uint64_t *q_com)
{
#ifdef NAIVE
	return write_match(length, payload, dst);
#endif


	if(*q_len > 0 && payload == *q_pay + *q_len && (payload + length < *q_com || !add_data) && *q_len + length <= 256*1024)
	{
		*q_len += length;
		return 0;
	}
	else
	{
		size_t r = cons_flush(dst, q_pay, q_len, q_com);
		*q_len = length;
		*q_pay = payload;
		return r;
	}
}

static size_t cons_literals(const unsigned char *src, size_t length, unsigned char *dst, int thread_id, uint64_t *q_pay, uint64_t *q_len, uint64_t *q_com)
{

#ifdef NAIVE
	return write_literals(src, length, dst, thread_id);
#endif
	unsigned char *orig_dst = dst;
	size_t original_length = length;
	dst += cons_flush(dst, q_pay, q_len, q_com);

	while(length > 0)
	{
		size_t process = minimum(256*1024, length);
		dst += write_literals(src, process, dst, thread_id);
		length -= process;
		src += process;
	}
	*q_com += original_length;
	return dst - orig_dst;
} 



INLINE static void hash_chunk(const unsigned char *src, uint64_t pay, size_t length)
{
	char tmp[8*KILO];
	size_t small_blocks = length / SMALL_BLOCK;
	uint32_t smalls = 0;
	uint32_t j = 0;

	int o = 2;

	for(j = 0; j < small_blocks; j++)
	{
		sha(src + j * SMALL_BLOCK, SMALL_BLOCK, (unsigned char *)tmp + smalls * SHA_SIZE);
		hashat(src + j * SMALL_BLOCK, pay + j * SMALL_BLOCK, SMALL_BLOCK, 0, (unsigned char *)tmp + smalls * SHA_SIZE, o);

		smalls++;
		if(smalls == LARGE_BLOCK / SMALL_BLOCK)
		{
			o = 0;
			unsigned char tmp2[SHA_SIZE];
			sha((unsigned char *)tmp, smalls * SHA_SIZE, tmp2);
			hashat(src + (j + 1) * SMALL_BLOCK - LARGE_BLOCK, pay + (j + 1) * SMALL_BLOCK - LARGE_BLOCK, LARGE_BLOCK, 1, (unsigned char *)tmp2, 1);

			smalls = 0;
		}
	}

	if(length < SMALL_BLOCK && length >= 128)
	{
		sha(src, length, (unsigned char *)tmp);
		hashat(src, pay, length, 0, (unsigned char *)tmp, 2);
	}

	if(length > SMALL_BLOCK && length < LARGE_BLOCK)
	{
		sha(src, length, (unsigned char *)tmp);
		hashat(src, pay, length, 1, (unsigned char *)tmp, 2);
	}

}

INLINE static size_t process_chunk(const unsigned char *src, uint64_t pay, size_t length, unsigned char *dst, int thread_id)
{
	size_t buffer = length;
	const unsigned char *last_valid = src + buffer - 1;
	const unsigned char *upto;
	const unsigned char *src_orig = src;
	unsigned char *dst_orig = dst;
	const unsigned char *last = src + length - 1;

	uint64_t q_pay = 0;
	uint64_t q_len = 0;
	uint64_t q_com = pay;

	while(src <= last)
	{
		uint64_t ref = 0;
		const unsigned char *match = 0;
		
		if(src + LARGE_BLOCK - 1 <= last_valid)
			match = dub(src, pay + (src - src_orig), last - src, LARGE_BLOCK, 1, &ref);
		upto = (match == 0 ? last : match - 1);
		
		while(src <= upto)
		{
			uint64_t ref_s = 0;
			const unsigned char *match_s = 0;

			if(src + SMALL_BLOCK - 1 <= last_valid) {
				match_s = dub(src, pay + (src - src_orig), (upto - src), SMALL_BLOCK, 0, &ref_s);
			}
			else if (src + 256 - 1 <= last_valid) {
				match_s = dub(src, pay + (src - src_orig), (upto - src), last_valid - src + 1, 0, &ref_s);				
			}


			if(match_s == 0)
			{
				dst += cons_literals(src, upto - src + 1, dst, thread_id, &q_pay, &q_len, &q_com);
				break;
			}
			else
			{
				if(match_s - src > 0)
				{
					dst += cons_literals(src, match_s - src, dst, thread_id, &q_pay, &q_len, &q_com);
				}
				dst += cons_match(minimum(SMALL_BLOCK, upto - match_s + 1), ref_s, dst, &q_pay, &q_len, &q_com);
				src = match_s + SMALL_BLOCK;
			}
		}

		if(match == 0)
		{
			dst += cons_flush(dst, &q_pay, &q_len, &q_com);
			return dst - dst_orig;
		}
		else
		{ 
			dst += cons_match(minimum(LARGE_BLOCK, last - match + 1), ref, dst, &q_pay, &q_len, &q_com);
			src = match + LARGE_BLOCK;
		}
	}

	dst += cons_flush(dst, &q_pay, &q_len, &q_com);
	return dst - dst_orig;
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


char inlined[DUP_MAX_INPUT];
uint64_t flushed;
uint64_t global_payload;
uint64_t count_payload;
uint64_t count_compressed;


INLINE static int get_free(void)
{
	int i;
	for(i = 0; i < THREADS; i++)
	{
		int r = pthread_mutex_trylock(&jobs[i].mutex);
		if(r == 0 && jobs[i].size_source + jobs[i].size_destination == 0)
			return i;
		if(r == 0)
			pthread_mutex_unlock(&jobs[i].mutex);
	}
	return -1;
}

pthread_t wakeup_thread;

static void *wakeup(void *)
{
	while(!exit_threads)
	{
		pthread_mutex_lock(&jobdone_mutex);
		pthread_cond_signal(&jobdone_cond);
		pthread_mutex_unlock(&jobdone_mutex);
		SLEEP1SEC
	}
	return 0;
}


INLINE static void *compress_thread(void *arg)
{
	job_t *me = (job_t *)arg;
	while(!exit_threads)
	{
		pthread_mutex_lock(&me->mutex);

		while(me->size_source == 0 || me->size_destination > 0)
		{
			me->busy = false;
			pthread_cond_wait(&me->cond, &me->mutex);
			if(exit_threads)
				return 0;
		}

		me->busy = true;

		if(me->add)
		{
			hash_chunk(me->source, me->payload, me->size_source);
		}

		me->size_destination = process_chunk(me->source, me->payload, me->size_source, me->destination, me->id);		
		
		me->busy = false;

		pthread_mutex_unlock(&me->mutex);

		pthread_mutex_lock(&jobdone_mutex);
		pthread_cond_signal(&jobdone_cond);
		pthread_mutex_unlock(&jobdone_mutex);
	}

	return 0;
}



uint64_t dup_memory(uint64_t bits)
{
	uint64_t t = sizeof(hash_t);
	return 2 * t * ((uint64_t)1 << bits);
}


int dup_init (size_t large_block, size_t small_block, uint64_t mem, int thread_count, void *space, int compression_level)
{
	//compile_assert(sizeof(hash_t) == 40);

	LEVEL = compression_level;
	bzip2d.bzalloc = 0;
	bzip2d.bzfree = 0;
	BZ2_bzDecompressInit(&bzip2d, 9, 0);

	int i;
	exit_threads = false;
	THREADS = thread_count;
	jobs = (job_t *)malloc(sizeof(job_t) * THREADS);
	if(!jobs)
		return 1;

#ifdef WINDOWS
	    pthread_win32_process_attach_np ();
#endif

	pthread_mutex_init(&table_mutex, NULL);
	pthread_mutex_init(&jobdone_mutex, NULL);
	pthread_cond_init(&jobdone_cond, NULL);

	SMALL_BLOCK = small_block;
	LARGE_BLOCK = large_block;

	HASH_ENTRIES = mem / (2*sizeof(hash_t));

//	std::wcerr << HASH_ENTRIES << " " << sizeof(hash_t);

	table = (hash_t (*)[2]) space;

	memset(table, 0, mem);

	global_payload = 0;
	flushed = 0;
	count_payload = 0;
	count_compressed = 0;

	for(i = 0; i < THREADS; i++)
	{
		pthread_mutex_init(&jobs[i].mutex, NULL);
		pthread_cond_init(&jobs[i].cond, NULL);
		jobs[i].id = i;
		jobs[i].size_destination = 0;
		jobs[i].size_source = 0;
		jobs[i].busy = false;
	}

	for(i = 0; i < THREADS; i++)
	{
		int t = pthread_create(&jobs[i].thread, NULL, compress_thread, &jobs[i]);
		if (t)
		{
			return 2;
		}
	}

	int t = pthread_create(&wakeup_thread, NULL, wakeup, 0);
	if(t)
		return 2;



	return 0;
}


void dup_deinit(void)
{
	int i;
	exit_threads = true;	

	for(i = 0; i < THREADS; i++)
	{
		pthread_mutex_lock(&jobs[i].mutex);
		pthread_cond_signal(&jobs[i].cond);
		pthread_mutex_unlock(&jobs[i].mutex);
		pthread_join(jobs[i].thread, 0);
	}

	if(jobs != 0)
		free(jobs);
}


size_t dup_size_compressed(const unsigned char *src)
{
	size_t t = str2ll(src + 8 - 6, 4);
	return t;
}


size_t dup_size_decompressed(const unsigned char *src)
{
	size_t t = str2ll(src + 16    -4      - 6, 4);
	return t;
}


uint64_t dup_counter_payload(void)
{
	return count_payload;
}

uint64_t dup_counter_compressed(void)
{
	return count_compressed;
}

void dup_counters_reset(void)
{
	count_payload = 0;
	count_compressed = 0;
}
	
INLINE static uint64_t packet_payload(const unsigned char *src)
{
	uint64_t t = str2ll(src + 24 - (6+8), 8);
	return t;
}

int dup_decompress(const unsigned char *src, unsigned char *dst, size_t *length, uint64_t *payload)
{
	if(dd_equal(src, DUP_LITERAL, 8 - 6))
	{
		size_t t;
		src += 32 - (6 + 8);

		if(*src == '0')
		{
			t = dup_size_decompressed(src - 32 + (6 + 8));
			memcpy(dst, src + 1, t);
		}
		else if(*src == '2')
		{
			zlibd.zalloc = 0;
			zlibd.zfree = 0;
			inflateInit(&zlibd);
			zlibd.avail_in = DUP_MAX_INPUT + 1024*1024;
			zlibd.avail_out = DUP_MAX_INPUT;
			zlibd.next_in = (Bytef *)src + 1;
			zlibd.next_out = (Bytef*)dst;
			int retv = inflate(&zlibd, Z_FINISH);
			if(retv != Z_STREAM_END) 
			{
				fprintf(stderr, "\neXdupe: Internal error or archive corrupted, at inflate()\n");
				exit(-1);
			}
			inflateEnd(&zlibd);
			t = zlibd.next_out - dst;
		}
		else if(*src == '3')
		{
			bzip2d.bzalloc = 0;
			bzip2d.bzfree = 0;
			BZ2_bzDecompressInit(&bzip2d, 0, 0);
			bzip2d.avail_in = DUP_MAX_INPUT + 1024*1024;
			bzip2d.avail_out = DUP_MAX_INPUT;
			bzip2d.next_in = (char *)src + 1;
			bzip2d.next_out = reinterpret_cast<char*>(dst);
			int retv = BZ2_bzDecompress(&bzip2d);
			if(retv != BZ_STREAM_END) 
			{
				fprintf(stderr, "\neXdupe: Internal error or archive corrupted, at BZ2_bzDecompress()\n");
				exit(-1);
			}
			BZ2_bzDecompressEnd(&bzip2d);
			t = bzip2d.next_out - reinterpret_cast<char*>(dst);
		}
		else if(*src == '1')
		{
			t = qlz_decompress(reinterpret_cast<const char*>(src) + 1, dst, &state_decompress);
		}
		else 
		{
			fprintf(stderr, "\neXdupe: Internal error or archive corrupted, missing compression level block header");
			exit(-1);
		}

		*length = t;
		count_payload += *length;
		if(t == 0)
			return -1;

		count_compressed += dup_size_compressed(src - 32 + (6+8));
		return 0;
	}
	if(dd_equal(src, DUP_MATCH, 8 - 6))
	{		
		uint64_t pay = packet_payload(src);
		size_t len = dup_size_decompressed(src);
		*payload = pay;
		*length = len;
		count_payload += *length;
		count_compressed += dup_size_compressed(src - 32 + (6+8));
		return 1;
	}
	else
	{
		return -2;
	}
}



int dup_decompress_simulate(const unsigned char *src, size_t *length, uint64_t *payload)
{
	if(dd_equal(src, DUP_LITERAL, 8 - 6))
	{
		size_t t;

		src += 8 - 6;
		src += 8 - 4;
		t = str2ll(src, 4);
	

		*length = t;
		if(t == 0)
			return -1;

		return 0;
	}
	if(dd_equal(src, DUP_MATCH, 8 - 6))
	{		
		uint64_t pay = packet_payload(src);
		size_t len = dup_size_decompressed(src);
		*payload = pay;
		*length = len;
		return 1;
	}
	else
	{
		return -2;
	}
}


INLINE static size_t flush_pend(char *dst)
{
	char *orig_dst = dst;
	bool again;

	do
	{
		int i;
		again = false;
		for(i = 0; i < THREADS; i++)
		{
			int r = pthread_mutex_trylock(&jobs[i].mutex);

			if(r == 0 && jobs[i].size_destination > 0 && jobs[i].payload == flushed)
			{
				memcpy(dst, jobs[i].destination, jobs[i].size_destination);
				dst += jobs[i].size_destination;
				flushed += jobs[i].size_source;
				jobs[i].size_destination = 0;
				jobs[i].size_source = 0;
				again = true;
				pthread_mutex_unlock(&jobs[i].mutex);
			}
			else if(r == 0)
				pthread_mutex_unlock(&jobs[i].mutex);
		}
	} while (again);

	return dst - orig_dst;
}

void dup_add(bool add)
{
	add_data = add;
}


INLINE size_t dup_compress2(const void *src, char *dst, size_t size, bool flush)
{	
	char *dst_orig = dst;

#if 0 // single threaded naive for debugging
	if(size > 0)
	{
		hash_chunk((unsigned char *)src, global_payload, size);
		size_t t = process_chunk((unsigned char *)src, global_payload, size, (unsigned char*)dst, 1);
		global_payload += size;
		return t;
	}
	else
		return 0;
#endif

	dst += flush_pend(dst);

	if(size > 0)
	{
		int f;

		pthread_mutex_lock(&jobdone_mutex);
		while ((f = get_free()) == -1)   // locks its mutex on success
		{	
			dst += flush_pend(dst);
			pthread_cond_wait(&jobdone_cond, &jobdone_mutex);
			dst += flush_pend(dst);
		}
		memcpy(jobs[f].source, src, size);
		jobs[f].payload = global_payload;
		global_payload += size;
		count_payload += size;
		jobs[f].size_source = size;
		jobs[f].add = add_data;
		pthread_mutex_unlock(&jobdone_mutex);

		pthread_cond_signal(&jobs[f].cond);
		pthread_mutex_unlock(&jobs[f].mutex);

	}

	if(flush)
	{
		while(flushed < global_payload)
		{
			dst += flush_pend(dst);
			utils_yield();
		}
	}

	return dst - dst_orig;
}

bool dup_busy(void) 
{
	int i;
	for(i = 0; i < THREADS; i++)
	{
		if(jobs[i].busy) {
			return true;
		}
	}
	return false;
}

size_t dup_compress(const void *src, unsigned char *dst, size_t size, bool flush)
{

	size_t len, s = 0, d = 0;
	do
	{
		len = (size > DUP_MAX_INPUT ? DUP_MAX_INPUT : size);

		bool flush2 = s + len < size ? false : flush;

		d += dup_compress2(static_cast<const char*>(src) + s, reinterpret_cast<char*>(dst) + d, len, flush2);
		s += len;
	} while(s < size);

	return d;
}



