// Copyright (c) 2025-2026, Brandon Lehmann
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// VAES (AVX-512) backend: 4-block-wide CTR pipeline using 512-bit operations

#if defined(__x86_64__) || defined(_M_X64)

#include "internal/aes_impl.h"
#include "internal/endian.h"

#include <cstring>
#include <immintrin.h>

namespace tinyaes
{
    namespace internal
    {

        void aes_ctr_pipeline_vaes(
            const uint32_t *rk,
            int rounds,
            const uint8_t *in,
            uint8_t *out,
            size_t blocks,
            uint8_t ctr[16])
        {
            const __m128i *rk128 = reinterpret_cast<const __m128i *>(rk);
            const __m128i bswap_mask = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

            __m128i ctr_block = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ctr));

            // Process 4 blocks at a time using ZMM registers
            size_t i = 0;
            for (; i + 4 <= blocks; i += 4)
            {
                // Generate 4 counter values
                __m128i ctr_le = _mm_shuffle_epi8(ctr_block, bswap_mask);
                const __m128i one = _mm_set_epi32(0, 0, 0, 1);
                __m128i c0 = ctr_block;
                __m128i c1_le = _mm_add_epi32(ctr_le, one);
                __m128i c2_le = _mm_add_epi32(c1_le, one);
                __m128i c3_le = _mm_add_epi32(c2_le, one);
                __m128i c1 = _mm_shuffle_epi8(c1_le, bswap_mask);
                __m128i c2 = _mm_shuffle_epi8(c2_le, bswap_mask);
                __m128i c3 = _mm_shuffle_epi8(c3_le, bswap_mask);

                // Pack into ZMM
                __m512i ctrs = _mm512_inserti32x4(
                    _mm512_inserti32x4(_mm512_inserti32x4(_mm512_castsi128_si512(c0), c1, 1), c2, 2), c3, 3);

                // Broadcast round key and XOR
                __m512i state = _mm512_xor_si512(ctrs, _mm512_broadcast_i32x4(rk128[0]));

                // Middle rounds
                for (int r = 1; r < rounds; ++r)
                {
                    state = _mm512_aesenc_epi128(state, _mm512_broadcast_i32x4(rk128[r]));
                }
                state = _mm512_aesenclast_epi128(state, _mm512_broadcast_i32x4(rk128[rounds]));

                // XOR with plaintext
                __m512i pt = _mm512_loadu_si512(in + i * 16);
                _mm512_storeu_si512(out + i * 16, _mm512_xor_si512(state, pt));

                // Advance counter by 4
                __m128i next_le = _mm_add_epi32(c3_le, one);
                ctr_block = _mm_shuffle_epi8(next_le, bswap_mask);
            }

            // Remaining blocks: fall back to single-block AES-NI
            for (; i < blocks; ++i)
            {
                __m128i b = _mm_xor_si128(ctr_block, rk128[0]);
                for (int r = 1; r < rounds; ++r)
                {
                    b = _mm_aesenc_si128(b, rk128[r]);
                }
                b = _mm_aesenclast_si128(b, rk128[rounds]);

                __m128i p = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in + i * 16));
                _mm_storeu_si128(reinterpret_cast<__m128i *>(out + i * 16), _mm_xor_si128(b, p));

                __m128i ctr_le = _mm_shuffle_epi8(ctr_block, bswap_mask);
                ctr_le = _mm_add_epi32(ctr_le, _mm_set_epi32(0, 0, 0, 1));
                ctr_block = _mm_shuffle_epi8(ctr_le, bswap_mask);
            }

            _mm_storeu_si128(reinterpret_cast<__m128i *>(ctr), ctr_block);
        }

    } // namespace internal
} // namespace tinyaes

#endif // x86_64
