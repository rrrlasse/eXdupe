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

// Portable AES implementation using T-tables (FIPS 197).
// NOTE: T-table implementations are susceptible to cache-timing side channels.
// For production use on x86, the AES-NI backend is preferred.

#include "internal/aes_impl.h"
#include "internal/endian.h"

#include <cstring>

namespace tinyaes
{
    namespace internal
    {

        // AES S-box (SubBytes)
        // clang-format off
        static const uint8_t sbox[256] = {
            0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
            0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
            0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
            0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
            0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
            0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
            0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
            0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
            0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
            0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
            0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
            0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
            0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
            0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
            0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
            0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
        };

        // Inverse S-box (InvSubBytes)
        static const uint8_t inv_sbox[256] = {
            0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
            0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
            0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
            0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
            0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
            0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
            0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
            0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
            0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
            0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
            0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
            0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
            0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
            0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
            0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
            0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
        };
        // clang-format on

        // GF(2^8) multiply by 2
        static inline uint8_t xtime(uint8_t x)
        {
            return static_cast<uint8_t>((x << 1) ^ (((x >> 7) & 1) * 0x1b));
        }

        // GF(2^8) multiply
        static inline uint8_t gmul(uint8_t a, uint8_t b)
        {
            uint8_t p = 0;
            for (int i = 0; i < 8; ++i)
            {
                if (b & 1)
                    p ^= a;
                uint8_t hi = a & 0x80;
                a = static_cast<uint8_t>(a << 1);
                if (hi)
                    a ^= 0x1b;
                b >>= 1;
            }
            return p;
        }

        // Encryption T-tables (Te0..Te3)
        // Te0[x] = S[x].[02, 01, 01, 03] (column of MixColumns * SubBytes)
        static uint32_t Te0[256], Te1[256], Te2[256], Te3[256];
        static uint32_t Td0[256], Td1[256], Td2[256], Td3[256];
        static void init_tables()
        {
            static const bool initialized = []()
            {
                for (int i = 0; i < 256; ++i)
                {
                    uint8_t s = sbox[i];
                    uint8_t s2 = xtime(s);
                    uint8_t s3 = static_cast<uint8_t>(s2 ^ s);

                    // Te0[i] = [s2, s, s, s3] as big-endian 32-bit word
                    Te0[i] = (static_cast<uint32_t>(s2) << 24) | (static_cast<uint32_t>(s) << 16)
                             | (static_cast<uint32_t>(s) << 8) | static_cast<uint32_t>(s3);
                    Te1[i] = (Te0[i] >> 8) | (Te0[i] << 24);
                    Te2[i] = (Te0[i] >> 16) | (Te0[i] << 16);
                    Te3[i] = (Te0[i] >> 24) | (Te0[i] << 8);

                    // Decryption tables
                    uint8_t si = inv_sbox[i];
                    uint8_t si9 = gmul(si, 0x09);
                    uint8_t sib = gmul(si, 0x0b);
                    uint8_t sid = gmul(si, 0x0d);
                    uint8_t sie = gmul(si, 0x0e);

                    Td0[i] = (static_cast<uint32_t>(sie) << 24) | (static_cast<uint32_t>(si9) << 16)
                             | (static_cast<uint32_t>(sid) << 8) | static_cast<uint32_t>(sib);
                    Td1[i] = (Td0[i] >> 8) | (Td0[i] << 24);
                    Td2[i] = (Td0[i] >> 16) | (Td0[i] << 16);
                    Td3[i] = (Td0[i] >> 24) | (Td0[i] << 8);
                }
                return true;
            }();
            (void)initialized;
        }

        // Round constants
        static const uint8_t rcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

        // Key expansion (FIPS 197 Section 5.2)
        void aes_key_expand_portable(const uint8_t *key, size_t key_len, uint32_t *rk)
        {
            init_tables();

            int nk = static_cast<int>(key_len / 4); // 4, 6, or 8
            int nr = nk + 6; // 10, 12, or 14
            int total = 4 * (nr + 1); // total words

            // Copy key into first Nk words
            for (int i = 0; i < nk; ++i)
            {
                rk[i] = load_be32(key + 4 * i);
            }

            for (int i = nk; i < total; ++i)
            {
                uint32_t temp = rk[i - 1];
                if (i % nk == 0)
                {
                    // RotWord + SubWord + Rcon
                    temp = (static_cast<uint32_t>(sbox[(temp >> 16) & 0xff]) << 24)
                           | (static_cast<uint32_t>(sbox[(temp >> 8) & 0xff]) << 16)
                           | (static_cast<uint32_t>(sbox[temp & 0xff]) << 8)
                           | (static_cast<uint32_t>(sbox[(temp >> 24) & 0xff]));
                    temp ^= static_cast<uint32_t>(rcon[i / nk]) << 24;
                }
                else if (nk > 6 && (i % nk == 4))
                {
                    // SubWord only for AES-256
                    temp = (static_cast<uint32_t>(sbox[(temp >> 24) & 0xff]) << 24)
                           | (static_cast<uint32_t>(sbox[(temp >> 16) & 0xff]) << 16)
                           | (static_cast<uint32_t>(sbox[(temp >> 8) & 0xff]) << 8)
                           | (static_cast<uint32_t>(sbox[temp & 0xff]));
                }
                rk[i] = rk[i - nk] ^ temp;
            }
        }

        void aes_encrypt_block_portable(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16])
        {
            init_tables();

            uint32_t s0 = load_be32(in) ^ rk[0];
            uint32_t s1 = load_be32(in + 4) ^ rk[1];
            uint32_t s2 = load_be32(in + 8) ^ rk[2];
            uint32_t s3 = load_be32(in + 12) ^ rk[3];

            uint32_t t0 = s0, t1 = s1, t2 = s2, t3 = s3;
            int r = 1;
            for (; r < rounds; ++r)
            {
                t0 = Te0[(s0 >> 24) & 0xff] ^ Te1[(s1 >> 16) & 0xff] ^ Te2[(s2 >> 8) & 0xff] ^ Te3[s3 & 0xff]
                     ^ rk[4 * r];
                t1 = Te0[(s1 >> 24) & 0xff] ^ Te1[(s2 >> 16) & 0xff] ^ Te2[(s3 >> 8) & 0xff] ^ Te3[s0 & 0xff]
                     ^ rk[4 * r + 1];
                t2 = Te0[(s2 >> 24) & 0xff] ^ Te1[(s3 >> 16) & 0xff] ^ Te2[(s0 >> 8) & 0xff] ^ Te3[s1 & 0xff]
                     ^ rk[4 * r + 2];
                t3 = Te0[(s3 >> 24) & 0xff] ^ Te1[(s0 >> 16) & 0xff] ^ Te2[(s1 >> 8) & 0xff] ^ Te3[s2 & 0xff]
                     ^ rk[4 * r + 3];
                s0 = t0;
                s1 = t1;
                s2 = t2;
                s3 = t3;
            }

            // Final round (no MixColumns)
            s0 = (static_cast<uint32_t>(sbox[(t0 >> 24) & 0xff]) << 24)
                 | (static_cast<uint32_t>(sbox[(t1 >> 16) & 0xff]) << 16)
                 | (static_cast<uint32_t>(sbox[(t2 >> 8) & 0xff]) << 8) | (static_cast<uint32_t>(sbox[t3 & 0xff]));
            s0 ^= rk[4 * rounds];

            s1 = (static_cast<uint32_t>(sbox[(t1 >> 24) & 0xff]) << 24)
                 | (static_cast<uint32_t>(sbox[(t2 >> 16) & 0xff]) << 16)
                 | (static_cast<uint32_t>(sbox[(t3 >> 8) & 0xff]) << 8) | (static_cast<uint32_t>(sbox[t0 & 0xff]));
            s1 ^= rk[4 * rounds + 1];

            s2 = (static_cast<uint32_t>(sbox[(t2 >> 24) & 0xff]) << 24)
                 | (static_cast<uint32_t>(sbox[(t3 >> 16) & 0xff]) << 16)
                 | (static_cast<uint32_t>(sbox[(t0 >> 8) & 0xff]) << 8) | (static_cast<uint32_t>(sbox[t1 & 0xff]));
            s2 ^= rk[4 * rounds + 2];

            s3 = (static_cast<uint32_t>(sbox[(t3 >> 24) & 0xff]) << 24)
                 | (static_cast<uint32_t>(sbox[(t0 >> 16) & 0xff]) << 16)
                 | (static_cast<uint32_t>(sbox[(t1 >> 8) & 0xff]) << 8) | (static_cast<uint32_t>(sbox[t2 & 0xff]));
            s3 ^= rk[4 * rounds + 3];

            store_be32(out, s0);
            store_be32(out + 4, s1);
            store_be32(out + 8, s2);
            store_be32(out + 12, s3);
        }

        // Apply InvMixColumns to a single round-key word using Td tables + sbox.
        // Td0[sbox[b]] = InvMixColumns column for byte b (since inv_sbox[sbox[b]] = b).
        static inline uint32_t inv_mix_column(uint32_t w)
        {
            return Td0[sbox[(w >> 24) & 0xff]] ^ Td1[sbox[(w >> 16) & 0xff]] ^ Td2[sbox[(w >> 8) & 0xff]]
                   ^ Td3[sbox[w & 0xff]];
        }

        void aes_decrypt_block_portable(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16])
        {
            init_tables();

            uint32_t s0 = load_be32(in) ^ rk[4 * rounds];
            uint32_t s1 = load_be32(in + 4) ^ rk[4 * rounds + 1];
            uint32_t s2 = load_be32(in + 8) ^ rk[4 * rounds + 2];
            uint32_t s3 = load_be32(in + 12) ^ rk[4 * rounds + 3];

            uint32_t t0 = s0, t1 = s1, t2 = s2, t3 = s3;
            int r = rounds - 1;
            for (; r > 0; --r)
            {
                // Equivalent Inverse Cipher (FIPS 197 §5.3.5):
                // Td tables combine InvSubBytes + InvMixColumns, so the round keys
                // for middle rounds must be preprocessed with InvMixColumns.
                uint32_t dk0 = inv_mix_column(rk[4 * r]);
                uint32_t dk1 = inv_mix_column(rk[4 * r + 1]);
                uint32_t dk2 = inv_mix_column(rk[4 * r + 2]);
                uint32_t dk3 = inv_mix_column(rk[4 * r + 3]);

                t0 = Td0[(s0 >> 24) & 0xff] ^ Td1[(s3 >> 16) & 0xff] ^ Td2[(s2 >> 8) & 0xff] ^ Td3[s1 & 0xff] ^ dk0;
                t1 = Td0[(s1 >> 24) & 0xff] ^ Td1[(s0 >> 16) & 0xff] ^ Td2[(s3 >> 8) & 0xff] ^ Td3[s2 & 0xff] ^ dk1;
                t2 = Td0[(s2 >> 24) & 0xff] ^ Td1[(s1 >> 16) & 0xff] ^ Td2[(s0 >> 8) & 0xff] ^ Td3[s3 & 0xff] ^ dk2;
                t3 = Td0[(s3 >> 24) & 0xff] ^ Td1[(s2 >> 16) & 0xff] ^ Td2[(s1 >> 8) & 0xff] ^ Td3[s0 & 0xff] ^ dk3;
                s0 = t0;
                s1 = t1;
                s2 = t2;
                s3 = t3;
            }

            // Final round (no InvMixColumns)
            s0 = (static_cast<uint32_t>(inv_sbox[(t0 >> 24) & 0xff]) << 24)
                 | (static_cast<uint32_t>(inv_sbox[(t3 >> 16) & 0xff]) << 16)
                 | (static_cast<uint32_t>(inv_sbox[(t2 >> 8) & 0xff]) << 8)
                 | (static_cast<uint32_t>(inv_sbox[t1 & 0xff]));
            s0 ^= rk[0];

            s1 = (static_cast<uint32_t>(inv_sbox[(t1 >> 24) & 0xff]) << 24)
                 | (static_cast<uint32_t>(inv_sbox[(t0 >> 16) & 0xff]) << 16)
                 | (static_cast<uint32_t>(inv_sbox[(t3 >> 8) & 0xff]) << 8)
                 | (static_cast<uint32_t>(inv_sbox[t2 & 0xff]));
            s1 ^= rk[1];

            s2 = (static_cast<uint32_t>(inv_sbox[(t2 >> 24) & 0xff]) << 24)
                 | (static_cast<uint32_t>(inv_sbox[(t1 >> 16) & 0xff]) << 16)
                 | (static_cast<uint32_t>(inv_sbox[(t0 >> 8) & 0xff]) << 8)
                 | (static_cast<uint32_t>(inv_sbox[t3 & 0xff]));
            s2 ^= rk[2];

            s3 = (static_cast<uint32_t>(inv_sbox[(t3 >> 24) & 0xff]) << 24)
                 | (static_cast<uint32_t>(inv_sbox[(t2 >> 16) & 0xff]) << 16)
                 | (static_cast<uint32_t>(inv_sbox[(t1 >> 8) & 0xff]) << 8)
                 | (static_cast<uint32_t>(inv_sbox[t0 & 0xff]));
            s3 ^= rk[3];

            store_be32(out, s0);
            store_be32(out + 4, s1);
            store_be32(out + 8, s2);
            store_be32(out + 12, s3);
        }

        // Portable CTR pipeline: encrypt blocks sequentially
        void aes_ctr_pipeline_portable(
            const uint32_t *rk,
            int rounds,
            const uint8_t *in,
            uint8_t *out,
            size_t blocks,
            uint8_t ctr[16])
        {
            uint8_t keystream[16];
            for (size_t i = 0; i < blocks; ++i)
            {
                aes_encrypt_block_portable(rk, rounds, ctr, keystream);
                for (size_t j = 0; j < 16; ++j)
                {
                    out[i * 16 + j] = in[i * 16 + j] ^ keystream[j];
                }
                increment_be32(ctr);
            }
        }

    } // namespace internal
} // namespace tinyaes
