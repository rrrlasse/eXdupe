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

    // CBC without padding — input must be multiple of 16 bytes
    TINYAES_EXPORT int tinyaes_cbc_encrypt(
        const uint8_t *key,
        size_t key_len,
        const uint8_t iv[16],
        const uint8_t *plaintext,
        size_t plaintext_len,
        uint8_t *ciphertext,
        size_t ciphertext_len);

    TINYAES_EXPORT int tinyaes_cbc_decrypt(
        const uint8_t *key,
        size_t key_len,
        const uint8_t iv[16],
        const uint8_t *ciphertext,
        size_t ciphertext_len,
        uint8_t *plaintext,
        size_t plaintext_len);

    // CBC with PKCS#7 padding
    TINYAES_EXPORT int tinyaes_cbc_encrypt_pkcs7(
        const uint8_t *key,
        size_t key_len,
        const uint8_t iv[16],
        const uint8_t *plaintext,
        size_t plaintext_len,
        uint8_t *ciphertext,
        size_t *ciphertext_len);

    TINYAES_EXPORT int tinyaes_cbc_decrypt_pkcs7(
        const uint8_t *key,
        size_t key_len,
        const uint8_t iv[16],
        const uint8_t *ciphertext,
        size_t ciphertext_len,
        uint8_t *plaintext,
        size_t *plaintext_len);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <vector>

namespace tinyaes
{

    Result cbc_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &ciphertext);

    Result cbc_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &ciphertext,
        std::vector<uint8_t> &plaintext);

    Result cbc_encrypt_pkcs7(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &ciphertext);

    Result cbc_decrypt_pkcs7(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &ciphertext,
        std::vector<uint8_t> &plaintext);

    // CBC encrypt with PKCS#7 — library generates IV, prepended to ciphertext
    Result cbc_encrypt_pkcs7(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &iv_and_ciphertext);

    // CBC decrypt with PKCS#7 — IV is first 16 bytes of input
    Result cbc_decrypt_pkcs7(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv_and_ciphertext,
        std::vector<uint8_t> &plaintext);

} // namespace tinyaes

#endif
