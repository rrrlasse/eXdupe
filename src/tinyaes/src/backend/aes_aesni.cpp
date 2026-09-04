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

// AES-NI (x86_64) backend: single-block encrypt/decrypt + pipelined CTR

#if defined(__x86_64__) || defined(_M_X64)

#include "internal/aes_impl.h"
#include "internal/endian.h"

#include <cstring>
#include <smmintrin.h> // SSE4.1
#include <wmmintrin.h> // AES-NI intrinsics

namespace tinyaes
{
    namespace internal
    {

        // Helper: AES-128 key expansion assist
        static inline __m128i aes_128_key_assist(__m128i key, __m128i gen)
        {
            gen = _mm_shuffle_epi32(gen, 0xFF);
            key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
            key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
            key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
            return _mm_xor_si128(key, gen);
        }

        // AES-NI key expansion
        void aes_key_expand_aesni(const uint8_t *key, size_t key_len, uint32_t *rk)
        {
            __m128i *rk128 = reinterpret_cast<__m128i *>(rk);

            if (key_len == 16)
            {
                // AES-128: 11 round keys
                __m128i k = _mm_loadu_si128(reinterpret_cast<const __m128i *>(key));
                rk128[0] = k;
                k = aes_128_key_assist(k, _mm_aeskeygenassist_si128(k, 0x01));
                rk128[1] = k;
                k = aes_128_key_assist(k, _mm_aeskeygenassist_si128(k, 0x02));
                rk128[2] = k;
                k = aes_128_key_assist(k, _mm_aeskeygenassist_si128(k, 0x04));
                rk128[3] = k;
                k = aes_128_key_assist(k, _mm_aeskeygenassist_si128(k, 0x08));
                rk128[4] = k;
                k = aes_128_key_assist(k, _mm_aeskeygenassist_si128(k, 0x10));
                rk128[5] = k;
                k = aes_128_key_assist(k, _mm_aeskeygenassist_si128(k, 0x20));
                rk128[6] = k;
                k = aes_128_key_assist(k, _mm_aeskeygenassist_si128(k, 0x40));
                rk128[7] = k;
                k = aes_128_key_assist(k, _mm_aeskeygenassist_si128(k, 0x80));
                rk128[8] = k;
                k = aes_128_key_assist(k, _mm_aeskeygenassist_si128(k, 0x1B));
                rk128[9] = k;
                k = aes_128_key_assist(k, _mm_aeskeygenassist_si128(k, 0x36));
                rk128[10] = k;
            }
            else
            {
                // For AES-192 and AES-256, use portable key expansion then convert
                // from big-endian uint32_t words to native byte order so that
                // _mm_loadu_si128 loads the correct key bytes for AES-NI.
                aes_key_expand_portable(key, key_len, rk);
                int nr = static_cast<int>(key_len / 4) + 6;
                int total_words = 4 * (nr + 1);
                for (int i = 0; i < total_words; ++i)
                {
                    uint32_t w = rk[i];
                    rk[i] =
                        ((w >> 24) & 0xFF) | ((w >> 8) & 0xFF00) | ((w << 8) & 0xFF0000) | ((w << 24) & 0xFF000000u);
                }
            }
        }

        void aes_encrypt_block_aesni(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16])
        {
            const __m128i *rk128 = reinterpret_cast<const __m128i *>(rk);
            __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in));

            block = _mm_xor_si128(block, rk128[0]);
            for (int i = 1; i < rounds; ++i)
            {
                block = _mm_aesenc_si128(block, rk128[i]);
            }
            block = _mm_aesenclast_si128(block, rk128[rounds]);

            _mm_storeu_si128(reinterpret_cast<__m128i *>(out), block);
        }

        void aes_decrypt_block_aesni(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16])
        {
            const __m128i *rk128 = reinterpret_cast<const __m128i *>(rk);

            // Need inverse round keys for AES-NI decrypt
            // InvMixColumns on round keys 1..rounds-1
            __m128i inv_rk[15];
            inv_rk[0] = rk128[rounds];
            for (int i = 1; i < rounds; ++i)
            {
                inv_rk[i] = _mm_aesimc_si128(rk128[rounds - i]);
            }
            inv_rk[rounds] = rk128[0];

            __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in));
            block = _mm_xor_si128(block, inv_rk[0]);
            for (int i = 1; i < rounds; ++i)
            {
                block = _mm_aesdec_si128(block, inv_rk[i]);
            }
            block = _mm_aesdeclast_si128(block, inv_rk[rounds]);

            _mm_storeu_si128(reinterpret_cast<__m128i *>(out), block);
        }

        // Pipelined CTR: process 4 blocks at a time
        void aes_ctr_pipeline_aesni(
            const uint32_t *rk,
            int rounds,
            const uint8_t *in,
            uint8_t *out,
            size_t blocks,
            uint8_t ctr[16])
        {
            const __m128i *rk128 = reinterpret_cast<const __m128i *>(rk);
            const __m128i one = _mm_set_epi32(0, 0, 0, 1);
            // AES counter is big-endian in last 4 bytes, but _mm_loadu loads LE
            // We need byte-swap for the counter increment
            const __m128i bswap_mask = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

            __m128i ctr_block = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ctr));

            // Process 4 blocks at a time
            size_t i = 0;
            for (; i + 4 <= blocks; i += 4)
            {
                __m128i c0 = ctr_block;
                __m128i c0_le = _mm_shuffle_epi8(c0, bswap_mask);
                __m128i c1_le = _mm_add_epi32(c0_le, one);
                __m128i c2_le = _mm_add_epi32(c1_le, one);
                __m128i c3_le = _mm_add_epi32(c2_le, one);
                __m128i c1 = _mm_shuffle_epi8(c1_le, bswap_mask);
                __m128i c2 = _mm_shuffle_epi8(c2_le, bswap_mask);
                __m128i c3 = _mm_shuffle_epi8(c3_le, bswap_mask);

                // Initial round key XOR
                __m128i b0 = _mm_xor_si128(c0, rk128[0]);
                __m128i b1 = _mm_xor_si128(c1, rk128[0]);
                __m128i b2 = _mm_xor_si128(c2, rk128[0]);
                __m128i b3 = _mm_xor_si128(c3, rk128[0]);

                // Middle rounds
                for (int r = 1; r < rounds; ++r)
                {
                    b0 = _mm_aesenc_si128(b0, rk128[r]);
                    b1 = _mm_aesenc_si128(b1, rk128[r]);
                    b2 = _mm_aesenc_si128(b2, rk128[r]);
                    b3 = _mm_aesenc_si128(b3, rk128[r]);
                }

                // Final round
                b0 = _mm_aesenclast_si128(b0, rk128[rounds]);
                b1 = _mm_aesenclast_si128(b1, rk128[rounds]);
                b2 = _mm_aesenclast_si128(b2, rk128[rounds]);
                b3 = _mm_aesenclast_si128(b3, rk128[rounds]);

                // XOR with plaintext
                __m128i p0 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in + i * 16));
                __m128i p1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in + i * 16 + 16));
                __m128i p2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in + i * 16 + 32));
                __m128i p3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in + i * 16 + 48));

                _mm_storeu_si128(reinterpret_cast<__m128i *>(out + i * 16), _mm_xor_si128(b0, p0));
                _mm_storeu_si128(reinterpret_cast<__m128i *>(out + i * 16 + 16), _mm_xor_si128(b1, p1));
                _mm_storeu_si128(reinterpret_cast<__m128i *>(out + i * 16 + 32), _mm_xor_si128(b2, p2));
                _mm_storeu_si128(reinterpret_cast<__m128i *>(out + i * 16 + 48), _mm_xor_si128(b3, p3));

                // Advance counter by 4
                __m128i next_le = _mm_add_epi32(c3_le, one);
                ctr_block = _mm_shuffle_epi8(next_le, bswap_mask);
            }

            // Process remaining blocks one at a time
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
                ctr_le = _mm_add_epi32(ctr_le, one);
                ctr_block = _mm_shuffle_epi8(ctr_le, bswap_mask);
            }

            // Store updated counter
            _mm_storeu_si128(reinterpret_cast<__m128i *>(ctr), ctr_block);
        }

    } // namespace internal
} // namespace tinyaes

#endif // x86_64
