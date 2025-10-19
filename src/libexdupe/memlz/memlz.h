// SPDX-License-Identifier: MIT
//
// memlz 0.1 beta - extremely fast header-only compression library for C and C++ on x64/x86
//
// Copyright 2025, Lasse Mikkel Reinhold
//
// Attributions: This is an 8-byte-word version of the Chameleon compression algorithmn
// by Guillaume Voirin. Thanks to Charles Bloom for his simple reference implementation and 
// for the original LZP-style of algorithms.

#ifndef memlz_h
#define memlz_h

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>


typedef struct memlz_state memlz_state;

/// Simple mode: First call memlz_reset(state) and then memlz_compress(). 
///
/// Streaming mode: First call memlz_reset(state) and then call memlz_compress() repeatedly.
/// Each call will always compress and ouptput the full input data given. There is no flush
/// function.
///
/// The destination buffer must be at least memlz_max_compressed_size(size) large.
static size_t memlz_compress(void* destination, const void* source, size_t size, memlz_state* state);

/// Simple mode: First call memlz_reset(state) and then memlz_decompress().
///
/// Streaming mode: First call memlz_reset(state) and then call memlz_decompress() repeatedly
/// in the same order for the compressed data as when you called memlz_compress(). 
//
/// The destination buffer must be at least memlz_decompressed_size(source) large.
static size_t memlz_decompress(void* destination, const void* source, memlz_state* state);

/// Takes compressed data as input and returns the decompressed size. Only the first
/// memlz_header_size() number of bytes need to present
static size_t memlz_compressed_size(const void* src);

/// Takes compressed data as input and returns the compressed size. Only the first
/// memlz_header_size() number of bytes need to present
static size_t memlz_decompressed_size(const void* src);

/// Return the largest number of bytes that a given input size can compress into. Note that
/// certain kinds of data may grow beyond its original size.
static size_t memlz_max_compressed_size(size_t input);

static size_t memlz_header_size();

///  Call this before the first call to memlz_compress() or memlz_decompress()
static void memlz_reset(memlz_state* c);

// The rest of this header file is internals
/////////////////////////////////////////////////////////////////////////////////////////////////////////

#define MEMLZ_RESTRICT __restrict

#define MEMLZ_UNROLL4(op) op; op; op; op
#define MEMLZ_UNROLL16(op) op; op; op; op; op; op; op; op; op; op; op; op; op; op; op; op;

#define MEMLZ_DECODE_WORD \
            if (flags & (1 << bitpos)) { \
                word = hash[*(uint16_t*)src]; \
                src += 2; \
            } else { \
                word = *((const memlz_word*)src); \
                src += sizeof(memlz_word); \
                hash[memlz_hash(word)] = word; \
            } \
            *(memlz_word*)dst = word; \
            dst += sizeof(memlz_word); \
            bitpos--;

typedef uint64_t memlz_word;

static const size_t memlz_header_fields = 2;
static const size_t memlz_bytes_per_word = sizeof(memlz_word);
static const size_t memlz_words_per_iteration = 16;
static const size_t memlz_bytes_per_iteration = memlz_words_per_iteration * memlz_bytes_per_word;

static uint16_t memlz_hash(memlz_word v) {
    if (sizeof(memlz_word) == 4) {
        return (uint16_t)(((v * 2654435761ull) >> 16));
    }
    else {
        return (uint16_t)(((v * 11400714819323198485ull) >> 48));
    }
}

static size_t memlz_equal(memlz_word value, const memlz_word* src, size_t max) {
    size_t n = 0;
    while (n < max && src[n] == value) {
        n++;
    }
    return n;
}

static uint64_t memlz_read(const void* src) {
    uint8_t* s = (uint8_t*)src;
    return *s != 0xff ? *s
        : *(uint16_t*)(s + 1) != 0xffff ? *(uint16_t*)(s + 1)
        : *(uint32_t*)(s + 3) != 0xffffffff ? *(uint32_t*)(s + 3)
        : *(uint64_t*)(s + 7);
}

static size_t memlz_bytes(const void* src) {
    uint8_t* s = (uint8_t*)src;
    return *s != 0xff ? 1 
        : *(uint16_t*)(s + 1) != 0xffff ? 3 
        : *(uint32_t*)(s + 3) != 0xffffffff ? 7 
        : 15;
}

static void memlz_write(void* dst, uint64_t value, size_t bytes) {
    assert(bytes == 1 || bytes == 3 || bytes == 7 || bytes == 15);

    uint8_t* d = (uint8_t*)dst;

    if (bytes == 1) {
        assert(value < 0xff);
        *d = (uint8_t)value;
    }
    else if (bytes == 3) {
        assert(value < 0xffff);
        memset(dst, 0xff, 1);
        *(uint16_t*)(d + 1) = (uint16_t)value;
    }
    else if (bytes == 7) {
        assert(value < 0xffffffff);
        memset(dst, 0xff, 3);
        *(uint32_t*)(d + 3) = (uint32_t)value;
    }
    else if (bytes == 15) {
        memset(dst, 0xff, 7);
        *(uint64_t*)(d + 7) = (uint64_t)value;
    }
}

static size_t memlz_fit(uint64_t value) {
    return value < 0xff ? 1 : value < 0xffff ? 3 : value < 0xffffffff ? 7 : 15;
}

typedef struct memlz_state {
    memlz_word hash[1 << 16];
    size_t total_input;
    size_t total_output;
    char reset;
} memlz_state;

static size_t memlz_max_compressed_size(size_t input) {
    return 68 * input / 64 + 100; // todo, find real bound
}

static size_t memlz_header_size() {
    return 17;
}

static void memlz_reset(memlz_state* c) {
    memset(c->hash, 0, sizeof(c->hash));
    c->total_input = 0;
    c->total_output = 0;
    c->reset = 'Y';
}

static size_t memlz_compress(void* MEMLZ_RESTRICT destination, const void* MEMLZ_RESTRICT source, size_t size, memlz_state* c) {
    if (c->reset != 'Y') {
        return 0;
    }

    const size_t max = memlz_max_compressed_size(size) > size ? memlz_max_compressed_size(size) : size;
    const size_t header_size = memlz_header_fields * memlz_fit(max);
    size_t missing = size;

    const uint8_t* src = (const uint8_t*)source;
    char* dst = (char*)destination;
    uint16_t flags = 0;
    dst += header_size;
    memlz_word* table = c->hash;

    while (missing >= memlz_bytes_per_iteration) {
#if 1
        size_t e = memlz_equal(*(memlz_word*)src, ((memlz_word*)src), missing / sizeof(memlz_word));
        if (e >= 4) {
            *dst++ = 2;
            size_t length = memlz_fit(e);
            memlz_write(dst, e, length);
            *(memlz_word*)(dst + length) = ((memlz_word*)src)[0];
            dst += sizeof(memlz_word) + length;
            missing -= e * sizeof(memlz_word);
            src += e * sizeof(memlz_word);
            continue;
        }
#endif        
        {
            memlz_word a, b, c, d;
            *dst++ = 0;
            uint16_t* flag_ptr = (uint16_t*)dst;
            dst += 2;

            #define MEMLZ_ENCODE_WORD(h, i, next) \
            flags <<= 1; \
            if (table[h] == ((memlz_word*)src)[i]) { \
                flags |= 1; \
                *(uint16_t*)dst = (uint16_t)h; \
                dst += 2; \
                next; \
            } else { \
                table[h] = ((memlz_word*)src)[i]; \
                * ((memlz_word*)dst) = ((memlz_word*)src)[i]; \
                dst += sizeof(memlz_word); \
                next; \
            }

            #define MEMLZ_ENCODE4(a,b,c,d) MEMLZ_ENCODE_WORD(a, 0, MEMLZ_ENCODE_WORD(b, 1, MEMLZ_ENCODE_WORD(c, 2, MEMLZ_ENCODE_WORD(d, 3, ))))

            MEMLZ_UNROLL4(a = memlz_hash(((memlz_word*)src)[0]); b = memlz_hash(((memlz_word*)src)[1]); c = memlz_hash(((memlz_word*)src)[2]); d = memlz_hash(((memlz_word*)src)[3]); \
                MEMLZ_ENCODE4(a, b, c, d); \
                src += 4 * sizeof(memlz_word);
            )

            *flag_ptr = (uint16_t)flags;
            missing -= memlz_bytes_per_iteration;
        }
#if 1
        size_t unc = 256;
        if (flags == 0 && missing >= unc) {
            *dst++ = 1;
            *(uint16_t*)dst = (uint16_t)unc;
            memlz_write(dst, unc, memlz_fit(unc));
            dst += memlz_fit(unc);
            memcpy(dst, src, unc);
            dst += unc;
            src = (uint8_t*)(((char*)src) + unc);
            missing -= unc;
        }
#endif
    }

    if (missing >= sizeof(memlz_word)) {
        uint16_t* flag_ptr = (uint16_t*)dst;
        dst += 2;
        flags = 0;
        int flags_left = memlz_words_per_iteration;

        while (missing >= sizeof(memlz_word)) {
            memlz_word a = memlz_hash(((memlz_word*)src)[0]);
            MEMLZ_ENCODE_WORD(a, 0, )
            src += sizeof(memlz_word);
            flags_left--;
            missing -= sizeof(memlz_word);
        }

        flags <<= flags_left;
        *flag_ptr = flags;
    }

    size_t tail_count = missing;
    memcpy(dst, src, tail_count);
    dst += tail_count;

    size_t compressed_size = ((char*)dst - (char*)destination);
    if (compressed_size < memlz_header_size()) {
        memset(dst, 'M', memlz_header_size() - compressed_size);
        compressed_size = memlz_header_size();
    }

    memlz_write(destination, size, header_size / memlz_header_fields);
    memlz_write((char*)destination + header_size / memlz_header_fields, compressed_size, header_size / memlz_header_fields);

    c->total_input += size;
    c->total_output += compressed_size;
    return compressed_size;
}


static size_t memlz_decompressed_size(const void* src) {
    return memlz_read(src);
}


static size_t memlz_compressed_size(const void* src) {
    size_t header_field_len = memlz_bytes(src);
    return memlz_read((char*)src + header_field_len);
}


static size_t memlz_decompress(void* MEMLZ_RESTRICT destination, const void* MEMLZ_RESTRICT source, memlz_state* c)
{
    if (c->reset != 'Y') {
        return 0;
    }

    const size_t decompressed_size = memlz_decompressed_size(source);
    const size_t compressed_size = memlz_compressed_size(source);
    size_t header_length = memlz_bytes(source) * memlz_header_fields;
    const uint8_t* src = (const uint8_t*)source + header_length;
    uint8_t* dst = (uint8_t*)destination;
    memlz_word word;
    memlz_word* hash = c->hash;
    size_t missing = decompressed_size;

    while (missing >= memlz_bytes_per_iteration)
    {
        char blocktype = *(char*)src;
        src++;
        
#if 1
        if (blocktype == 1) {
            size_t unc = memlz_read(src);
            src += memlz_bytes(src);
            memcpy(dst, src, unc);
            dst += unc;
            src += unc;
            missing -= unc;
            continue;
        }
#endif

#if 1        
        if (blocktype == 2) {
            size_t len = memlz_bytes(src);
            uint64_t z = memlz_read(src);
            src += len;

            uint64_t v = *((uint64_t*)src);
            src += sizeof(memlz_word);

            for (uint64_t n = 0; n < z; n++) {
                ((memlz_word*)dst)[n] = v;
            }
            dst += z * sizeof(memlz_word);
            missing -= z * sizeof(memlz_word);
            continue;
        }
#endif
        if (blocktype != 0) {
            return 0;
        }

        const uint16_t flags = *(uint16_t*)src;
        src += 2;
        int bitpos = memlz_words_per_iteration - 1;

        MEMLZ_UNROLL16(MEMLZ_DECODE_WORD)
        missing -= (sizeof(memlz_word) * 16);
    }


    if (missing >= sizeof(memlz_word)) {
        const uint16_t flags = *(uint16_t*)src;
        src += 2;

        int bitpos = memlz_words_per_iteration - 1;
        while (missing >= sizeof(memlz_word))
        {
            MEMLZ_DECODE_WORD;
            missing -= sizeof(memlz_word);
        }
    }


    size_t tail_count = missing;
    memcpy(dst, src, tail_count);
    
    c->total_input += compressed_size;
    c->total_output += decompressed_size;
    return decompressed_size;
}

#undef MEMLZ_UNROLL4
#undef MEMLZ_UNROLL16
#undef MEMLZ_ENCODE_WORD
#undef MEMLZ_DECODE_WORD
#undef MEMLZ_VOID
#undef memlz_word

#endif // memlz_h