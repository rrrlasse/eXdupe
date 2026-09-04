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

// PCLMULQDQ GHASH implementation for x86_64

#if defined(__x86_64__) || defined(_M_X64)

#include "internal/ghash.h"

#include <cstring>
#include <emmintrin.h>
#include <smmintrin.h>
#include <wmmintrin.h>

namespace tinyaes
{
    namespace internal
    {

        // GF(2^128) multiplication using PCLMULQDQ with Karatsuba method
        // and two-phase reduction (Intel CLMUL whitepaper "Algorithm 5").
        // GCM polynomial: x^128 + x^7 + x^2 + x + 1
        // Reflected: x^128 + x^127 + x^126 + x^121 + 1
        // Low 64 bits of reflected poly (excl. x^128 and x^0): 0xC200000000000000
        static inline void gf128_mul_pclmul(const __m128i a, const __m128i b, __m128i &result)
        {
            // Karatsuba carry-less multiplication → 256-bit product [hi:lo]
            __m128i lo = _mm_clmulepi64_si128(a, b, 0x00);
            __m128i hi = _mm_clmulepi64_si128(a, b, 0x11);
            __m128i mid0 = _mm_clmulepi64_si128(a, b, 0x01);
            __m128i mid1 = _mm_clmulepi64_si128(a, b, 0x10);
            __m128i mid = _mm_xor_si128(mid0, mid1);

            hi = _mm_xor_si128(hi, _mm_srli_si128(mid, 8));
            lo = _mm_xor_si128(lo, _mm_slli_si128(mid, 8));

            // Two-phase reduction. The constant 0xC200000000000000 must be in
            // the LOW 64 bits of the __m128i so that selector 0x00 picks it up.
            const __m128i poly = _mm_set_epi64x(0, static_cast<long long>(0xC200000000000000ULL));

            // Phase 1: multiply lo's low 64 bits by the polynomial constant
            __m128i t1 = _mm_clmulepi64_si128(lo, poly, 0x00);
            // Swap lo's 64-bit halves (handles the implicit x^64 term) and XOR
            lo = _mm_xor_si128(_mm_shuffle_epi32(lo, 0x4E), t1);

            // Phase 2: repeat on the intermediate result
            __m128i t2 = _mm_clmulepi64_si128(lo, poly, 0x00);
            lo = _mm_xor_si128(_mm_shuffle_epi32(lo, 0x4E), t2);

            // Combine with the high half
            result = _mm_xor_si128(lo, hi);
        }

        void ghash_pclmulqdq(const uint8_t H[16], const uint8_t *data, size_t data_len, uint8_t Y[16])
        {
            const __m128i bswap_mask = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

            __m128i h = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i *>(H)), bswap_mask);

            // Pre-shift H left by 1 bit in the reflected domain to compensate for
            // the x factor introduced by CLMUL's polynomial multiplication convention.
            // This is the standard GHASH-CLMUL preprocessing step.
            __m128i h_carry = _mm_srli_epi64(h, 63); // bit 63 of each half
            __m128i h_carry_low = _mm_slli_si128(h_carry, 8); // carry from low to high
            __m128i h_overflow = _mm_srli_si128(h_carry, 8); // overflow bit (was bit 127)
            h = _mm_or_si128(_mm_slli_epi64(h, 1), h_carry_low);
            // If bit 127 was set, reduce: x^128 mod p_reflected = x^127+x^126+x^121+1
            const __m128i reduce_const = _mm_set_epi64x(static_cast<long long>(0xC200000000000000ULL), 1);
            // Broadcast overflow to both 64-bit halves for a full 128-bit mask
            __m128i reduce_mask = _mm_shuffle_epi32(_mm_sub_epi64(_mm_setzero_si128(), h_overflow), 0x44); // [lo, lo]
            h = _mm_xor_si128(h, _mm_and_si128(reduce_const, reduce_mask));

            __m128i y = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i *>(Y)), bswap_mask);

            size_t full_blocks = data_len / 16;
            for (size_t i = 0; i < full_blocks; ++i)
            {
                __m128i d =
                    _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i * 16)), bswap_mask);
                y = _mm_xor_si128(y, d);
                gf128_mul_pclmul(y, h, y);
            }

            // Handle partial block
            size_t remainder = data_len % 16;
            if (remainder > 0)
            {
                uint8_t padded[16] = {0};
                std::memcpy(padded, data + full_blocks * 16, remainder);
                __m128i d = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i *>(padded)), bswap_mask);
                y = _mm_xor_si128(y, d);
                gf128_mul_pclmul(y, h, y);
            }

            _mm_storeu_si128(reinterpret_cast<__m128i *>(Y), _mm_shuffle_epi8(y, bswap_mask));
        }

    } // namespace internal
} // namespace tinyaes

#endif // x86_64
