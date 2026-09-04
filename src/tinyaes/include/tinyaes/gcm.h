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

    TINYAES_EXPORT int tinyaes_gcm_encrypt(
        const uint8_t *key,
        size_t key_len,
        const uint8_t *iv,
        size_t iv_len,
        const uint8_t *aad,
        size_t aad_len,
        const uint8_t *plaintext,
        size_t plaintext_len,
        uint8_t *ciphertext,
        size_t ciphertext_len,
        uint8_t tag[16]);

    TINYAES_EXPORT int tinyaes_gcm_decrypt(
        const uint8_t *key,
        size_t key_len,
        const uint8_t *iv,
        size_t iv_len,
        const uint8_t *aad,
        size_t aad_len,
        const uint8_t *ciphertext,
        size_t ciphertext_len,
        uint8_t *plaintext,
        size_t plaintext_len,
        const uint8_t tag[16]);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <vector>

namespace tinyaes
{

    Result gcm_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &aad,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &ciphertext,
        std::vector<uint8_t> &tag);

    Result gcm_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &aad,
        const std::vector<uint8_t> &ciphertext,
        const std::vector<uint8_t> &tag,
        std::vector<uint8_t> &plaintext);

    // GCM encrypt — caller provides nonce, tag appended to ciphertext
    Result gcm_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &nonce,
        const std::vector<uint8_t> &plaintext,
        const std::vector<uint8_t> &aad,
        std::vector<uint8_t> &ciphertext_and_tag);

    // GCM encrypt — library generates nonce, output is nonce||ciphertext||tag
    Result gcm_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &plaintext,
        const std::vector<uint8_t> &aad,
        std::vector<uint8_t> &nonce_ciphertext_tag);

    // GCM decrypt — caller provides nonce, input is ciphertext||tag
    Result gcm_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &nonce,
        const std::vector<uint8_t> &ciphertext_and_tag,
        const std::vector<uint8_t> &aad,
        std::vector<uint8_t> &plaintext);

    // GCM decrypt — nonce is first 12 bytes of input, rest is ciphertext||tag
    Result gcm_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &nonce_ciphertext_tag,
        const std::vector<uint8_t> &aad,
        std::vector<uint8_t> &plaintext);

} // namespace tinyaes

#endif
