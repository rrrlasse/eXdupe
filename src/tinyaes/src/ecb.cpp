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

#include "tinyaes/ecb.h"

#include "internal/aes_impl.h"

#include <cstring>

namespace tinyaes
{

    Result ecb_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &ciphertext)
    {
        int rounds = internal::aes_rounds(key.size());
        if (rounds == 0)
            return Result::InvalidKeySize;
        if (plaintext.empty() || (plaintext.size() % 16) != 0)
            return Result::InvalidInputSize;

        uint32_t rk[internal::AES_MAX_RK_WORDS];
        auto key_expand = internal::get_key_expand();
        key_expand(key.data(), key.size(), rk);

        auto encrypt_block = internal::get_encrypt_block();
        ciphertext.resize(plaintext.size());

        for (size_t i = 0; i < plaintext.size(); i += 16)
        {
            encrypt_block(rk, rounds, plaintext.data() + i, ciphertext.data() + i);
        }

        secure_zero(rk, sizeof(rk));
        return Result::Ok;
    }

    Result ecb_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &ciphertext,
        std::vector<uint8_t> &plaintext)
    {
        int rounds = internal::aes_rounds(key.size());
        if (rounds == 0)
            return Result::InvalidKeySize;
        if (ciphertext.empty() || (ciphertext.size() % 16) != 0)
            return Result::InvalidInputSize;

        uint32_t rk[internal::AES_MAX_RK_WORDS];
        auto key_expand = internal::get_key_expand();
        key_expand(key.data(), key.size(), rk);

        auto decrypt_block = internal::get_decrypt_block();
        plaintext.resize(ciphertext.size());

        for (size_t i = 0; i < ciphertext.size(); i += 16)
        {
            decrypt_block(rk, rounds, ciphertext.data() + i, plaintext.data() + i);
        }

        secure_zero(rk, sizeof(rk));
        return Result::Ok;
    }

} // namespace tinyaes

extern "C" int tinyaes_ecb_encrypt(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *plaintext,
    size_t plaintext_len,
    uint8_t *ciphertext,
    size_t ciphertext_len)
{
    if (!key || !plaintext || !ciphertext)
        return TINYAES_INVALID_INPUT_SIZE;
    if (ciphertext_len < plaintext_len)
        return TINYAES_INVALID_INPUT_SIZE;

    std::vector<uint8_t> k(key, key + key_len);
    std::vector<uint8_t> pt(plaintext, plaintext + plaintext_len);
    std::vector<uint8_t> ct;

    auto result = tinyaes::ecb_encrypt(k, pt, ct);
    tinyaes::secure_zero(k.data(), k.size());
    tinyaes::secure_zero(pt.data(), pt.size());

    if (result != tinyaes::Result::Ok)
        return static_cast<int>(result);

    std::memcpy(ciphertext, ct.data(), ct.size());
    return TINYAES_OK;
}

extern "C" int tinyaes_ecb_decrypt(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *ciphertext,
    size_t ciphertext_len,
    uint8_t *plaintext,
    size_t plaintext_len)
{
    if (!key || !ciphertext || !plaintext)
        return TINYAES_INVALID_INPUT_SIZE;
    if (plaintext_len < ciphertext_len)
        return TINYAES_INVALID_INPUT_SIZE;

    std::vector<uint8_t> k(key, key + key_len);
    std::vector<uint8_t> ct(ciphertext, ciphertext + ciphertext_len);
    std::vector<uint8_t> pt;

    auto result = tinyaes::ecb_decrypt(k, ct, pt);
    tinyaes::secure_zero(k.data(), k.size());

    if (result != tinyaes::Result::Ok)
    {
        tinyaes::secure_zero(pt.data(), pt.size());
        return static_cast<int>(result);
    }

    std::memcpy(plaintext, pt.data(), pt.size());
    tinyaes::secure_zero(pt.data(), pt.size());
    return TINYAES_OK;
}
