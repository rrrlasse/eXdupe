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

#include "tinyaes/common.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // CTR encrypt/decrypt (symmetric operation) — raw 16-byte IV
    TINYAES_EXPORT int tinyaes_ctr_crypt(
        const uint8_t *key,
        size_t key_len,
        const uint8_t iv[16],
        const uint8_t *input,
        size_t input_len,
        uint8_t *output,
        size_t output_len);

    // CTR encrypt — 12-byte nonce (counter starts at 1)
    TINYAES_EXPORT int tinyaes_ctr_encrypt(
        const uint8_t *key,
        size_t key_len,
        const uint8_t *nonce,
        const uint8_t *plaintext,
        size_t plaintext_len,
        uint8_t *ciphertext,
        size_t ciphertext_len);

    // CTR decrypt — 12-byte nonce
    TINYAES_EXPORT int tinyaes_ctr_decrypt(
        const uint8_t *key,
        size_t key_len,
        const uint8_t *nonce,
        const uint8_t *ciphertext,
        size_t ciphertext_len,
        uint8_t *plaintext,
        size_t plaintext_len);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <vector>

namespace tinyaes
{

    // CTR mode: encrypt and decrypt are the same operation
    Result ctr_crypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &input,
        std::vector<uint8_t> &output);

    // CTR encrypt — caller provides nonce (12 bytes, counter starts at 1)
    Result ctr_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &nonce,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &ciphertext);

    // CTR encrypt — library generates nonce, prepended to output
    Result ctr_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &nonce_and_ciphertext);

    // CTR decrypt — caller provides nonce
    Result ctr_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &nonce,
        const std::vector<uint8_t> &ciphertext,
        std::vector<uint8_t> &plaintext);

    // CTR decrypt — nonce is first 12 bytes of input
    Result ctr_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &nonce_and_ciphertext,
        std::vector<uint8_t> &plaintext);

} // namespace tinyaes

#endif
