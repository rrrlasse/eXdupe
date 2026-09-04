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

// ARM Crypto Extensions AES backend

#if defined(__aarch64__) || defined(_M_ARM64)

#include "internal/aes_impl.h"
#include "internal/endian.h"

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

        // ARM AES: vaeseq + vaesmcq for encryption, vaesdq + vaesimcq for decryption
        // Note: ARM AES instructions combine SubBytes+ShiftRows (vaese) and MixColumns
        // (vaesmc) The key XOR is done separately (veorq).

        // Round keys are stored as big-endian uint32_t by the portable key expansion.
        // On little-endian ARM64, the bytes within each 32-bit word are reversed in
        // memory. vrev32q_u8 restores the original byte order expected by the ARM AES
        // instructions.
        static inline uint8x16_t load_rk(const uint8_t *rk8)
        {
            return vrev32q_u8(vld1q_u8(rk8));
        }

        void aes_encrypt_block_arm_ce(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16])
        {
            const uint8_t *rk8 = reinterpret_cast<const uint8_t *>(rk);
            uint8x16_t block = vld1q_u8(in);

            // ARM vaese does: AddRoundKey ^ SubBytes ^ ShiftRows
            // So we need: vaese(block, key[i]) then vaesmcq for MixColumns
            for (int i = 0; i < rounds - 1; ++i)
            {
                uint8x16_t key = load_rk(rk8 + i * 16);
                block = vaeseq_u8(block, key);
                block = vaesmcq_u8(block);
            }
            // Last round: no MixColumns
            uint8x16_t key_last = load_rk(rk8 + (rounds - 1) * 16);
            block = vaeseq_u8(block, key_last);
            // Final AddRoundKey
            uint8x16_t key_final = load_rk(rk8 + rounds * 16);
            block = veorq_u8(block, key_final);

            vst1q_u8(out, block);
        }

        void aes_decrypt_block_arm_ce(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16])
        {
            const uint8_t *rk8 = reinterpret_cast<const uint8_t *>(rk);
            uint8x16_t block = vld1q_u8(in);

            // ARM AESD places AddRoundKey before InvSubBytes, but standard AES
            // places it after. Since InvSubBytes is nonlinear, the middle round
            // keys must be pre-processed with InvMixColumns to compensate.
            // First round key (rk[rounds]) and last round key (rk[0]) are used as-is.
            block = vaesdq_u8(block, load_rk(rk8 + rounds * 16));
            block = vaesimcq_u8(block);

            for (int i = rounds - 1; i > 1; --i)
            {
                block = vaesdq_u8(block, vaesimcq_u8(load_rk(rk8 + i * 16)));
                block = vaesimcq_u8(block);
            }
            // Last round: key needs InvMixColumns, but no InvMixColumns on state
            block = vaesdq_u8(block, vaesimcq_u8(load_rk(rk8 + 16)));
            // Final AddRoundKey
            block = veorq_u8(block, load_rk(rk8));

            vst1q_u8(out, block);
        }

        void aes_ctr_pipeline_arm_ce(
            const uint32_t *rk,
            int rounds,
            const uint8_t *in,
            uint8_t *out,
            size_t blocks,
            uint8_t ctr[16])
        {
            const uint8_t *rk8 = reinterpret_cast<const uint8_t *>(rk);

            for (size_t i = 0; i < blocks; ++i)
            {
                uint8x16_t block = vld1q_u8(ctr);

                for (int r = 0; r < rounds - 1; ++r)
                {
                    uint8x16_t key = load_rk(rk8 + r * 16);
                    block = vaeseq_u8(block, key);
                    block = vaesmcq_u8(block);
                }
                uint8x16_t key_last = load_rk(rk8 + (rounds - 1) * 16);
                block = vaeseq_u8(block, key_last);
                uint8x16_t key_final = load_rk(rk8 + rounds * 16);
                block = veorq_u8(block, key_final);

                // XOR with plaintext
                uint8x16_t pt = vld1q_u8(in + i * 16);
                vst1q_u8(out + i * 16, veorq_u8(block, pt));

                increment_be32(ctr);
            }
        }

    } // namespace internal
} // namespace tinyaes

#endif // aarch64
