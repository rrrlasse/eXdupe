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

        // GHASH function signature: processes AAD and ciphertext, produces 16-byte tag
        // component H is the hash key (AES_K(0^128)), Y is the running accumulator
        using ghash_fn = void (*)(const uint8_t H[16], const uint8_t *data, size_t data_len, uint8_t Y[16]);

        // Portable GHASH
        void ghash_portable(const uint8_t H[16], const uint8_t *data, size_t data_len, uint8_t Y[16]);

        // x86 PCLMULQDQ GHASH
        void ghash_pclmulqdq(const uint8_t H[16], const uint8_t *data, size_t data_len, uint8_t Y[16]);

        // x86 VPCLMULQDQ GHASH (AVX-512)
        void ghash_vpclmulqdq(const uint8_t H[16], const uint8_t *data, size_t data_len, uint8_t Y[16]);

        // ARM PMULL GHASH
        void ghash_arm_ce(const uint8_t H[16], const uint8_t *data, size_t data_len, uint8_t Y[16]);

        // Dispatch getter
        ghash_fn get_ghash();

    } // namespace internal
} // namespace tinyaes
