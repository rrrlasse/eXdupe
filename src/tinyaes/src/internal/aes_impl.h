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

#pragma once

#include <cstddef>
#include <cstdint>

namespace tinyaes
{
    namespace internal
    {

        // Maximum round keys: AES-256 has 15 round keys * 4 words = 60 words
        static constexpr size_t AES_MAX_RK_WORDS = 60;

        // Backend function signatures
        using encrypt_block_fn = void (*)(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]);
        using decrypt_block_fn = void (*)(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]);
        using key_expand_fn = void (*)(const uint8_t *key, size_t key_len, uint32_t *rk);
        using ctr_pipeline_fn =
            void (*)(const uint32_t *rk, int rounds, const uint8_t *in, uint8_t *out, size_t blocks, uint8_t ctr[16]);

        // Portable backend declarations
        void aes_encrypt_block_portable(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]);
        void aes_decrypt_block_portable(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]);
        void aes_key_expand_portable(const uint8_t *key, size_t key_len, uint32_t *rk);
        void aes_ctr_pipeline_portable(
            const uint32_t *rk,
            int rounds,
            const uint8_t *in,
            uint8_t *out,
            size_t blocks,
            uint8_t ctr[16]);

        // x86 AES-NI backend declarations
        void aes_encrypt_block_aesni(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]);
        void aes_decrypt_block_aesni(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]);
        void aes_key_expand_aesni(const uint8_t *key, size_t key_len, uint32_t *rk);
        void aes_ctr_pipeline_aesni(
            const uint32_t *rk,
            int rounds,
            const uint8_t *in,
            uint8_t *out,
            size_t blocks,
            uint8_t ctr[16]);

        // x86 VAES (AVX-512) backend declarations
        void aes_ctr_pipeline_vaes(
            const uint32_t *rk,
            int rounds,
            const uint8_t *in,
            uint8_t *out,
            size_t blocks,
            uint8_t ctr[16]);

        // ARM CE backend declarations
        void aes_encrypt_block_arm_ce(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]);
        void aes_decrypt_block_arm_ce(const uint32_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]);
        void aes_ctr_pipeline_arm_ce(
            const uint32_t *rk,
            int rounds,
            const uint8_t *in,
            uint8_t *out,
            size_t blocks,
            uint8_t ctr[16]);

        // Dispatch getters (lazy-resolved via CPUID)
        encrypt_block_fn get_encrypt_block();
        decrypt_block_fn get_decrypt_block();
        key_expand_fn get_key_expand();
        ctr_pipeline_fn get_ctr_pipeline();

        // Number of rounds for a given key size
        inline int aes_rounds(size_t key_len)
        {
            switch (key_len)
            {
                case 16:
                    return 10;
                case 24:
                    return 12;
                case 32:
                    return 14;
                default:
                    return 0;
            }
        }

    } // namespace internal
} // namespace tinyaes
