#ifndef GXHASH_H
#define GXHASH_H

#include <stdint.h>

//#define GXHASH_NEON
//#define GXHASH_VAES
#define GXHASH_SSE2

#if defined(GXHASH_SSE2) || defined(GXHASH_VAES)
#include <immintrin.h>
#endif

#if defined(GXHASH_VAES)
typedef __m256i gxhash_register;
#else
typedef __m128i gxhash_register;
#endif

typedef struct {
    gxhash_register internal_state;
    gxhash_register finalized;
    size_t read;
    uint32_t seed;
} gxhash_state;


#if defined (__cplusplus)
extern "C" {
#endif

// Only last call is allowed to have unaligned size
void gxhash_stream(const uint8_t *input, size_t len, gxhash_state *state);
void gxhash(const uint8_t *input, size_t len, char *dst, size_t result_len, uint32_t hash_seed);
void gxhash_finish(gxhash_state *);
void gxhash_init(gxhash_state *, uint32_t hash_seed);

#if defined (__cplusplus)
}
#endif

#endif // GXHASH_H
