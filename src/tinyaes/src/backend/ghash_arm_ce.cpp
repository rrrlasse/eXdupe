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

// ARM PMULL GHASH implementation (vmull_p64)

#if defined(__aarch64__) || defined(_M_ARM64)

#include "internal/ghash.h"

#include <cstring>

#if defined(_MSC_VER)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif

namespace tinyaes
{
    namespace internal
    {

        // GF(2^128) multiplication using PMULL (carry-less multiply)
        // Inputs/outputs are in standard bit order (vrbitq_u8 applied):
        //   lane 0 = x^0..x^63 (low), lane 1 = x^64..x^127 (high)
        static inline uint8x16_t gf128_mul_pmull(uint8x16_t a, uint8x16_t b)
        {
            poly64_t a_lo = vgetq_lane_p64(vreinterpretq_p64_u8(a), 0);
            poly64_t a_hi = vgetq_lane_p64(vreinterpretq_p64_u8(a), 1);
            poly64_t b_lo = vgetq_lane_p64(vreinterpretq_p64_u8(b), 0);
            poly64_t b_hi = vgetq_lane_p64(vreinterpretq_p64_u8(b), 1);

            // Karatsuba multiplication: A(x)*B(x) where A = a_hi*x^64 + a_lo
            poly128_t lo = vmull_p64(a_lo, b_lo);
            poly128_t hi = vmull_p64(a_hi, b_hi);
            poly128_t mid0 = vmull_p64(a_lo, b_hi);
            poly128_t mid1 = vmull_p64(a_hi, b_lo);

            uint8x16_t lo_v = vreinterpretq_u8_p128(lo);
            uint8x16_t hi_v = vreinterpretq_u8_p128(hi);
            uint8x16_t mid = veorq_u8(vreinterpretq_u8_p128(mid0), vreinterpretq_u8_p128(mid1));

            // Combine mid*x^64 into [hi_v : lo_v]
            uint8x16_t mid_lo = vextq_u8(vdupq_n_u8(0), mid, 8); // mid << 64
            uint8x16_t mid_hi = vextq_u8(mid, vdupq_n_u8(0), 8); // mid >> 64
            lo_v = veorq_u8(lo_v, mid_lo);
            hi_v = veorq_u8(hi_v, mid_hi);

            // 256-bit product = [D3:D2:D1:D0] where hi_v=[D3:D2], lo_v=[D1:D0]
            // Reduction modulo p(x) = x^128 + x^7 + x^2 + x + 1
            // Since x^128 ≡ x^7+x^2+x+1, let q = 0x87
            poly64_t q = (poly64_t)0x87ULL;

            // Step 1: Reduce D3 — D3*x^192 ≡ (D3*q)*x^64
            poly128_t t1 = vmull_p64(vgetq_lane_p64(vreinterpretq_p64_u8(hi_v), 1), q);
            uint8x16_t t1_v = vreinterpretq_u8_p128(t1);
            lo_v = veorq_u8(lo_v, vextq_u8(vdupq_n_u8(0), t1_v, 8)); // D1 ^= T1_lo
            hi_v = veorq_u8(hi_v, vextq_u8(t1_v, vdupq_n_u8(0), 8)); // D2 ^= T1_hi

            // Step 2: Reduce D2' — D2'*x^128 ≡ D2'*q
            poly128_t t2 = vmull_p64(vgetq_lane_p64(vreinterpretq_p64_u8(hi_v), 0), q);
            lo_v = veorq_u8(lo_v, vreinterpretq_u8_p128(t2));

            return lo_v;
        }

        void ghash_arm_ce(const uint8_t H[16], const uint8_t *data, size_t data_len, uint8_t Y[16])
        {
            // GCM uses MSB-first bit ordering (bit 7 of byte 0 = x^0), but ARM PMULL
            // uses LSB-first (bit 0 = x^0). vrbitq_u8 reverses bits within each byte
            // to convert between these conventions.
            uint8x16_t h = vrbitq_u8(vld1q_u8(H));
            uint8x16_t y = vrbitq_u8(vld1q_u8(Y));

            size_t full_blocks = data_len / 16;
            for (size_t i = 0; i < full_blocks; ++i)
            {
                uint8x16_t d = vrbitq_u8(vld1q_u8(data + i * 16));
                y = veorq_u8(y, d);
                y = gf128_mul_pmull(y, h);
            }

            size_t remainder = data_len % 16;
            if (remainder > 0)
            {
                uint8_t padded[16] = {0};
                std::memcpy(padded, data + full_blocks * 16, remainder);
                uint8x16_t d = vrbitq_u8(vld1q_u8(padded));
                y = veorq_u8(y, d);
                y = gf128_mul_pmull(y, h);
            }

            vst1q_u8(Y, vrbitq_u8(y));
        }

    } // namespace internal
} // namespace tinyaes

#endif // aarch64
