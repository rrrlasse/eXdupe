#include "gxhash.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <stdint.h>
#include <string.h>

static const uint8_t sbox[256] = {0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36,
                                  0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b,
                                  0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f,
                                  0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc,
                                  0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c,
                                  0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1,
                                  0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

static __m128i aesenc_emulated(__m128i state, __m128i roundKey) {
    unsigned char b[16];
    unsigned char out[16];
    unsigned char s[16];

    _mm_storeu_si128((__m128i *)b, state);

    s[0] = sbox[b[0]];
    s[1] = sbox[b[5]];
    s[2] = sbox[b[10]];
    s[3] = sbox[b[15]];
    s[4] = sbox[b[4]];
    s[5] = sbox[b[9]];
    s[6] = sbox[b[14]];
    s[7] = sbox[b[3]];
    s[8] = sbox[b[8]];
    s[9] = sbox[b[13]];
    s[10] = sbox[b[2]];
    s[11] = sbox[b[7]];
    s[12] = sbox[b[12]];
    s[13] = sbox[b[1]];
    s[14] = sbox[b[6]];
    s[15] = sbox[b[11]];

    for (int i = 0; i < 4; i++) {
        unsigned char a0 = s[i * 4 + 0];
        unsigned char a1 = s[i * 4 + 1];
        unsigned char a2 = s[i * 4 + 2];
        unsigned char a3 = s[i * 4 + 3];

        unsigned char x0 = (unsigned char)(((a0 << 1) ^ ((a0 & 0x80) ? 0x1B : 0)) & 0xFF);
        unsigned char x1 = (unsigned char)(((a1 << 1) ^ ((a1 & 0x80) ? 0x1B : 0)) & 0xFF);
        unsigned char x2 = (unsigned char)(((a2 << 1) ^ ((a2 & 0x80) ? 0x1B : 0)) & 0xFF);
        unsigned char x3 = (unsigned char)(((a3 << 1) ^ ((a3 & 0x80) ? 0x1B : 0)) & 0xFF);

        out[i * 4 + 0] = (unsigned char)(x0 ^ a1 ^ x1 ^ a2 ^ a3);
        out[i * 4 + 1] = (unsigned char)(a0 ^ x1 ^ a2 ^ x2 ^ a3);
        out[i * 4 + 2] = (unsigned char)(a0 ^ a1 ^ x2 ^ a3 ^ x3);
        out[i * 4 + 3] = (unsigned char)(a0 ^ x0 ^ a1 ^ a2 ^ x3);
    }

    __m128i mixed = _mm_loadu_si128((__m128i *)out);

    return _mm_xor_si128(mixed, roundKey);
}

static __m128i aesenc_last_emulated(__m128i block, __m128i roundKey) {
    uint8_t input[16];

    _mm_storeu_si128((__m128i *)input, block);

    for (int i = 0; i < 16; i++)
        input[i] = sbox[input[i]];

    uint8_t temp[16];
    temp[0] = input[0];
    temp[1] = input[5];
    temp[2] = input[10];
    temp[3] = input[15];
    temp[4] = input[4];
    temp[5] = input[9];
    temp[6] = input[14];
    temp[7] = input[3];
    temp[8] = input[8];
    temp[9] = input[13];
    temp[10] = input[2];
    temp[11] = input[7];
    temp[12] = input[12];
    temp[13] = input[1];
    temp[14] = input[6];
    temp[15] = input[11];

    _mm_storeu_si128((__m128i *)input, roundKey);
    for (int i = 0; i < 16; i++)
        temp[i] ^= input[i];

    return _mm_loadu_si128((__m128i *)temp);
}

#if defined(GXHASH_VAES)

static inline gxhash_register create_empty() {
    return _mm256_setzero_si256();
}

static inline gxhash_register load_unaligned(const gxhash_register* p) {
    return _mm256_loadu_si256(p);
}

static inline int check_same_page(const gxhash_register* ptr) {
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t offset_within_page = address & 0xFFF;
    return offset_within_page <= (4096 - sizeof(gxhash_register) - 1);
}

static inline gxhash_register get_partial_safe(const uint8_t* data, size_t len) {
    uint8_t buffer[sizeof(gxhash_register)] = {0};
    memcpy(buffer, data, len);
    return _mm256_loadu_si256((const gxhash_register*)buffer);
}

static inline gxhash_register get_partial(const gxhash_register* p, intptr_t len) {
    gxhash_register partial;
    if (check_same_page(p)) {
        // Unsafe (hence the check) but much faster
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        gxhash_register indices = _mm256_set_epi8(31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
#else
        gxhash_register indices = _mm256_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
#endif
        gxhash_register mask = _mm256_cmpgt_epi8(_mm256_set1_epi8((char)len), indices);
        partial = _mm256_and_si256(_mm256_loadu_si256(p), mask);
    } else {
        // Safer but slower, using memcpy
        partial = get_partial_safe((const uint8_t*)p, (size_t)len);
    }
    
    // Prevents padded zeroes to introduce bias
    return _mm256_add_epi32(partial, _mm256_set1_epi32((char)len));
}

static inline gxhash_register compress(gxhash_register a, gxhash_register b) {
    gxhash_register keys_1 = _mm256_set_epi32(0xFC3BC28E, 0x89C222E5, 0xB09D3E21, 0xF2784542, 0x4155EE07, 0xC897CCE2, 0x780AF2C3, 0x8A72B781);
    gxhash_register keys_2 = _mm256_set_epi32(0x03FCE279, 0xCB6B2E9B, 0xB361DC58, 0x39136BD9, 0x7A83D76B, 0xB1E8F9F0, 0x028925A8, 0x3B9A4E71);

    b = _mm256_aesenc_epi128(b, keys_1);
    b = _mm256_aesenc_epi128(b, keys_2);

    return _mm256_aesenclast_epi128(a, b);
}

static inline gxhash_register compress_fast(gxhash_register a, gxhash_register b) {
    return _mm256_aesenc_epi128(a, b);
}

static inline gxhash_register finalize(gxhash_register* hash, uint32_t hash_seed) {
    gxhash_register keys_1 = _mm256_set_epi32(0x713B01D0, 0x8F2F35DB, 0xAF163956, 0x85459F85, 0xB49D3E21, 0xF2784542, 0x2155EE07, 0xC197CCE2);
    gxhash_register keys_2 = _mm256_set_epi32(0x1DE09647, 0x92CFA39C, 0x3DD99ACA, 0xB89C054F, 0xCB6B2E9B, 0xC361DC58, 0x39136BD9, 0x7A83D76F);
    gxhash_register keys_3 = _mm256_set_epi32(0xC78B122B, 0x5544B1B7, 0x689D2B7D, 0xD0012E32, 0xE2784542, 0x4155EE07, 0xC897CCE2, 0x780BF2C2);

    *hash = _mm256_aesenc_epi128(*hash, _mm256_set1_epi32(hash_seed));
    *hash = _mm256_aesenc_epi128(*hash, keys_1);
    *hash = _mm256_aesenc_epi128(*hash, keys_2);
    *hash = _mm256_aesenclast_epi128(*hash, keys_3);

    return *hash;
}
#elif defined(GXHASH_SSE2)


gxhash_register create_empty() {
    return _mm_setzero_si128();
}

gxhash_register load_unaligned(const gxhash_register* p) {
    return _mm_loadu_si128(p);
}

gxhash_register get_partial_safe(const uint8_t* data, size_t len) {
    uint8_t buffer[sizeof(gxhash_register)] = {0};
    memcpy(buffer, data, len);
    return _mm_loadu_si128((const gxhash_register*)buffer);
}

int are_equal_m128i(__m128i a, __m128i b) {
    __m128i result = _mm_cmpeq_epi32(a, b);

    // Extract the results. If all are -1, then all 32-bit elements were equal.
    int mask = _mm_movemask_epi8(result);
    return mask == 0xFFFF; // all 16 bytes were equal
}

gxhash_register get_partial(const gxhash_register* p, size_t len) {
    gxhash_register partial;
    partial = get_partial_safe((const uint8_t*)p, (size_t)len);
    // Prevents padded zeroes to introduce bias
    return _mm_add_epi8(partial, _mm_set1_epi8((char)len));
}

gxhash_register compress(gxhash_register a, gxhash_register b) {
    gxhash_register keys_1 = _mm_set_epi32(0xF2784542, 0xB09D3E21, 0x89C222E5, 0xFC3BC28E);
    gxhash_register keys_2 = _mm_set_epi32(0x39136BD9, 0xB361DC58, 0xCB6B2E9B, 0x03FCE279);
    b = aesenc_emulated(b, keys_1);
    b = aesenc_emulated(b, keys_2);
    return aesenc_last_emulated(a, b);
}

gxhash_register compress_avx(gxhash_register a, gxhash_register b) {
    gxhash_register keys_1 = _mm_set_epi32(0xF2784542, 0xB09D3E21, 0x89C222E5, 0xFC3BC28E);
    gxhash_register keys_2 = _mm_set_epi32(0x39136BD9, 0xB361DC58, 0xCB6B2E9B, 0x03FCE279);
    b = _mm_aesenc_si128(b, keys_1);
    b = _mm_aesenc_si128(b, keys_2);
    return _mm_aesenclast_si128(a, b);
}

static inline gxhash_register compress_fast(gxhash_register a, gxhash_register b) {
    return aesenc_emulated(a, b);
}

static inline gxhash_register compress_fast_avx(gxhash_register a, gxhash_register b) {
    return _mm_aesenc_si128(a, b);
}

static inline void finalize(gxhash_register *hash, uint32_t hash_seed, bool use_avx) {
    gxhash_register keys_1 = _mm_set_epi32(0xE37845F2, 0xB09D2F61, 0x89F216D5, 0x5A3BC47E);
    gxhash_register keys_2 = _mm_set_epi32(0x3D423129, 0xDE3A74DB, 0x6EA75BBA, 0xE7554D6F);
    gxhash_register keys_3 = _mm_set_epi32(0x444DF600, 0x790FC729, 0xA735B3F2, 0xC992E848);
    if (use_avx) {
        *hash = _mm_aesenc_si128(*hash, _mm_set1_epi32(hash_seed + 0xC992E848));
        *hash = _mm_aesenc_si128(*hash, keys_1);
        *hash = _mm_aesenc_si128(*hash, keys_2);
        *hash = _mm_aesenclast_si128(*hash, keys_3);
    } else {
        *hash = aesenc_emulated(*hash, _mm_set1_epi32(hash_seed + 0xC992E848));
        *hash = aesenc_emulated(*hash, keys_1);
        *hash = aesenc_emulated(*hash, keys_2); // bad
        *hash = aesenc_last_emulated(*hash, keys_3);
    }
}

#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#include <arm_neon.h>

typedef int8x16_t gxhash_register;

static inline gxhash_register create_empty() {
    return vdupq_n_s8(0);
}

static inline gxhash_register load_unaligned(const gxhash_register* p) {
    return vld1q_s8((const int8_t*)p);
}

static inline int check_same_page(const gxhash_register* ptr) {
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t offset_within_page = address & 0xFFF;
    return offset_within_page <= (4096 - sizeof(gxhash_register) - 1);
}

static inline gxhash_register get_partial(const gxhash_register* p, int len) {
    int8x16_t partial;
    if (check_same_page(p)) {
        // Unsafe (hence the check) but much faster
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        static const int8_t indices[] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
#else
        static const int8_t indices[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
#endif
        int8x16_t mask = vcgtq_s8(vdupq_n_u8(len), vld1q_s8(indices));
        partial = vandq_s8(load_unaligned(p), mask);
    }
    else {
        // Safer but slower, using memcpy
        uint8_t buffer[sizeof(gxhash_register)] = { 0 };
        memcpy(buffer, (const uint8_t*)p, len);
        partial = vld1q_s8((const int8_t*)buffer);
    }

    // Prevents padded zeroes to introduce bias
    return vaddq_u8(partial, vdupq_n_u8(len));
}

static inline uint8x16_t aes_encrypt(uint8x16_t data, uint8x16_t keys) {
    uint8x16_t encrypted = vaeseq_u8(data, vdupq_n_u8(0));
    uint8x16_t mixed = vaesmcq_u8(encrypted);
    return veorq_u8(mixed, keys);
}

static inline uint8x16_t aes_encrypt_last(uint8x16_t data, uint8x16_t keys) {
    uint8x16_t encrypted = vaeseq_u8(data, vdupq_n_u8(0));
    return veorq_u8(encrypted, keys);
}

static inline void compress(gxhash_register a, gxhash_register b) {
    static const uint32_t keys_1[4] = { 0xFC3BC28E, 0x89C222E5, 0xB09D3E21, 0xF2784542 };
    static const uint32_t keys_2[4] = { 0x03FCE279, 0xCB6B2E9B, 0xB361DC58, 0x39136BD9 };

    b = aes_encrypt(b, vld1q_u32(keys_1));
    b = aes_encrypt(b, vld1q_u32(keys_2));

    return aes_encrypt_last(a, b);
}

static inline gxhash_register compress_fast(gxhash_register a, gxhash_register b) {
    return aes_encrypt(a, b);
}

static inline gxhash_register finalize(gxhash_register hash, uint32_t hash_seed) {
    static const uint32_t keys_1[4] = { 0x5A3BC47E, 0x89F216D5, 0xB09D2F61, 0xE37845F2 };
    static const uint32_t keys_2[4] = { 0xE7554D6F, 0x6EA75BBA, 0xDE3A74DB, 0x3D423129 };
    static const uint32_t keys_3[4] = { 0xC992E848, 0xA735B3F2, 0x790FC729, 0x444DF600 };

    hash = aes_encrypt(hash, vdupq_n_u32(hash_seed + 0xC992E848));
    hash = aes_encrypt(hash, vld1q_u32(keys_1));
    hash = aes_encrypt(hash, vld1q_u32(keys_2));
    hash = aes_encrypt_last(hash, vld1q_u32(keys_3));
}

#endif


void gxhash_init(gxhash_state *s, uint32_t hash_seed, bool use_aesni) {
    s->read = 0;
    s->internal_state = create_empty();
    s->seed = hash_seed;
    s->use_aesni = use_aesni;
}

void gxhash_stream(const uint8_t* input, size_t len, gxhash_state* state) {
    if (len == 0) {
        return;
    }

    const size_t VECTOR_SIZE = sizeof(gxhash_register);
    const gxhash_register* v = (const gxhash_register*)input;
    const gxhash_register* end_address;
    size_t remaining_blocks_count = len / VECTOR_SIZE;
    const size_t UNROLL_FACTOR = 8;

    assert(len == 0 || state->read % (VECTOR_SIZE * UNROLL_FACTOR) == 0);
    state->read += len;

    if (len <= VECTOR_SIZE && state->read == 0) {
        state->internal_state = get_partial(v, len);
        return;
    }

    if (len >= VECTOR_SIZE * UNROLL_FACTOR) {
        size_t unrollable_blocks_count = (len / (VECTOR_SIZE * UNROLL_FACTOR)) * UNROLL_FACTOR;
        end_address = v + unrollable_blocks_count;

        if (state->use_aesni) {
            while (v < end_address) {
                gxhash_register v0 = load_unaligned(v++);
                gxhash_register v1 = load_unaligned(v++);
                gxhash_register v2 = load_unaligned(v++);
                gxhash_register v3 = load_unaligned(v++);
                gxhash_register v4 = load_unaligned(v++);
                gxhash_register v5 = load_unaligned(v++);
                gxhash_register v6 = load_unaligned(v++);
                gxhash_register v7 = load_unaligned(v++);
                v0 = compress_fast_avx(v0, v1);
                v0 = compress_fast_avx(v0, v2);
                v0 = compress_fast_avx(v0, v3);
                v0 = compress_fast_avx(v0, v4);
                v0 = compress_fast_avx(v0, v5);
                v0 = compress_fast_avx(v0, v6);
                v0 = compress_fast_avx(v0, v7);
                state->internal_state = compress_avx(state->internal_state, v0);
            }
        } 
        else {
            while (v < end_address) {
                gxhash_register v0 = load_unaligned(v++);
                gxhash_register v1 = load_unaligned(v++);
                gxhash_register v2 = load_unaligned(v++);
                gxhash_register v3 = load_unaligned(v++);
                gxhash_register v4 = load_unaligned(v++);
                gxhash_register v5 = load_unaligned(v++);
                gxhash_register v6 = load_unaligned(v++);
                gxhash_register v7 = load_unaligned(v++);
                v0 = compress_fast(v0, v1);
                v0 = compress_fast(v0, v2);
                v0 = compress_fast(v0, v3);
                v0 = compress_fast(v0, v4);
                v0 = compress_fast(v0, v5);
                v0 = compress_fast(v0, v6);
                v0 = compress_fast(v0, v7);
                state->internal_state = compress(state->internal_state, v0);
            }
        }

        remaining_blocks_count -= unrollable_blocks_count;
    }

    end_address = v + remaining_blocks_count;

    while (v < end_address) {
        gxhash_register v0 = load_unaligned(v++);
        state->internal_state = compress(state->internal_state, v0);
    }

    size_t remaining_bytes = len % VECTOR_SIZE;
    if (remaining_bytes > 0) {
        gxhash_register partial_vector = get_partial(v, remaining_bytes);
        state->internal_state = compress(state->internal_state, partial_vector);
    }
}


void gxhash(const uint8_t* input, size_t len, char* dst, size_t result_len, uint32_t hash_seed, bool use_aesni) {
    gxhash_state state;
    gxhash_init(&state, hash_seed, use_aesni);
    gxhash_stream(input, len, &state);
    gxhash_finish(&state);
    memcpy(dst, &state.finalized, result_len);
}


void gxhash_finish(gxhash_state* s) {
    s->finalized = s->internal_state;
    finalize(&s->finalized, s->seed, s->use_aesni);
}
