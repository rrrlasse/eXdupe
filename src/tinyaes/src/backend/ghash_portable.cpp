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

// Portable constant-time GHASH implementation in GF(2^128).
// MSB-first convention, reduction polynomial x^128 + x^7 + x^2 + x + 1 (0xE1).

#include "internal/endian.h"
#include "internal/ghash.h"

namespace tinyaes
{
    namespace internal
    {

        // GF(2^128) multiplication: Z = X * Y in GF(2^128)
        // Uses bit-by-bit algorithm (constant-time: no data-dependent branches).
        static void gf128_mul(const uint8_t X[16], const uint8_t Y[16], uint8_t Z[16])
        {
            uint64_t Xh = load_be64(X);
            uint64_t Xl = load_be64(X + 8);
            uint64_t Zh = 0, Zl = 0;
            uint64_t Vh = load_be64(Y);
            uint64_t Vl = load_be64(Y + 8);

            for (int i = 0; i < 128; ++i)
            {
                // If bit i of X is set, XOR V into Z
                // Constant-time: compute mask from bit value
                uint64_t xi;
                if (i < 64)
                    xi = (Xh >> (63 - i)) & 1;
                else
                    xi = (Xl >> (127 - i)) & 1;

                uint64_t mask = static_cast<uint64_t>(0) - xi; // 0 or 0xFFFFFFFFFFFFFFFF
                Zh ^= (Vh & mask);
                Zl ^= (Vl & mask);

                // V = V >> 1 in GF(2^128) with reduction
                uint64_t carry = Vl & 1;
                Vl = (Vl >> 1) | (Vh << 63);
                Vh >>= 1;
                // If carry, XOR with R = 0xE1 << 56 (0xE100000000000000)
                uint64_t reduce_mask = static_cast<uint64_t>(0) - carry;
                Vh ^= (UINT64_C(0xE100000000000000) & reduce_mask);
            }

            store_be64(Z, Zh);
            store_be64(Z + 8, Zl);
        }

        void ghash_portable(const uint8_t H[16], const uint8_t *data, size_t data_len, uint8_t Y[16])
        {
            // Process complete 16-byte blocks
            size_t full_blocks = data_len / 16;
            for (size_t i = 0; i < full_blocks; ++i)
            {
                // Y = (Y XOR data_block) * H
                for (int j = 0; j < 16; ++j)
                {
                    Y[j] ^= data[i * 16 + static_cast<size_t>(j)];
                }
                uint8_t tmp[16];
                gf128_mul(Y, H, tmp);
                for (int j = 0; j < 16; ++j)
                {
                    Y[j] = tmp[j];
                }
            }

            // Handle final partial block (zero-padded)
            size_t remainder = data_len % 16;
            if (remainder > 0)
            {
                for (size_t j = 0; j < remainder; ++j)
                {
                    Y[j] ^= data[full_blocks * 16 + j];
                }
                // Remaining bytes stay as-is (zero-padded XOR is identity)
                uint8_t tmp[16];
                gf128_mul(Y, H, tmp);
                for (int j = 0; j < 16; ++j)
                {
                    Y[j] = tmp[j];
                }
            }
        }

    } // namespace internal
} // namespace tinyaes
