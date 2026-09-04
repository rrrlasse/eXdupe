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

#include "tinyaes/cbc.h"

#include "internal/aes_impl.h"

#include <cstring>

namespace tinyaes
{

    Result cbc_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &ciphertext)
    {
        int rounds = internal::aes_rounds(key.size());
        if (rounds == 0)
            return Result::InvalidKeySize;
        if (iv.size() != 16)
            return Result::InvalidIVSize;
        if (plaintext.empty() || (plaintext.size() % 16) != 0)
            return Result::InvalidInputSize;

        uint32_t rk[internal::AES_MAX_RK_WORDS];
        auto key_expand = internal::get_key_expand();
        key_expand(key.data(), key.size(), rk);

        auto encrypt_block = internal::get_encrypt_block();
        ciphertext.resize(plaintext.size());

        uint8_t block[16];
        std::memcpy(block, iv.data(), 16);

        for (size_t i = 0; i < plaintext.size(); i += 16)
        {
            // XOR plaintext block with previous ciphertext (or IV)
            for (int j = 0; j < 16; ++j)
            {
                block[j] ^= plaintext[i + static_cast<size_t>(j)];
            }
            encrypt_block(rk, rounds, block, ciphertext.data() + i);
            std::memcpy(block, ciphertext.data() + i, 16);
        }

        secure_zero(rk, sizeof(rk));
        secure_zero(block, sizeof(block));
        return Result::Ok;
    }

    Result cbc_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &ciphertext,
        std::vector<uint8_t> &plaintext)
    {
        int rounds = internal::aes_rounds(key.size());
        if (rounds == 0)
            return Result::InvalidKeySize;
        if (iv.size() != 16)
            return Result::InvalidIVSize;
        if (ciphertext.empty() || (ciphertext.size() % 16) != 0)
            return Result::InvalidInputSize;

        uint32_t rk[internal::AES_MAX_RK_WORDS];
        auto key_expand = internal::get_key_expand();
        key_expand(key.data(), key.size(), rk);

        auto decrypt_block = internal::get_decrypt_block();
        plaintext.resize(ciphertext.size());

        const uint8_t *prev = iv.data();

        for (size_t i = 0; i < ciphertext.size(); i += 16)
        {
            decrypt_block(rk, rounds, ciphertext.data() + i, plaintext.data() + i);
            // XOR with previous ciphertext block (or IV)
            for (int j = 0; j < 16; ++j)
            {
                plaintext[i + static_cast<size_t>(j)] ^= prev[j];
            }
            prev = ciphertext.data() + i;
        }

        secure_zero(rk, sizeof(rk));
        return Result::Ok;
    }

    // PKCS#7 padding: append N bytes of value N where N = 16 - (len % 16)
    // If input is already block-aligned, a full padding block is added.
    Result cbc_encrypt_pkcs7(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &ciphertext)
    {
        size_t pad_len = 16 - (plaintext.size() % 16);
        std::vector<uint8_t> padded(plaintext.size() + pad_len);
        // Guard: libc memcpy's src is declared nonnull, so calling it with
        // an empty vector's data() (which may return nullptr) is UB even
        // when n == 0. UBSan correctly flags it.
        if (!plaintext.empty())
            std::memcpy(padded.data(), plaintext.data(), plaintext.size());
        std::memset(padded.data() + plaintext.size(), static_cast<unsigned char>(pad_len), pad_len);

        auto result = cbc_encrypt(key, iv, padded, ciphertext);
        secure_zero(padded.data(), padded.size());
        return result;
    }

    // Constant-time PKCS#7 unpadding
    Result cbc_decrypt_pkcs7(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &ciphertext,
        std::vector<uint8_t> &plaintext)
    {
        std::vector<uint8_t> decrypted;
        auto result = cbc_decrypt(key, iv, ciphertext, decrypted);
        if (result != Result::Ok)
            return result;

        if (decrypted.empty())
            return Result::InvalidPadding;

        // Fully constant-time PKCS#7 validation: always scan all 16 bytes of last
        // block
        uint8_t pad_val = decrypted.back();

        // Range check via arithmetic (no branches):
        // bad_range is 0xFF if pad_val is out of [1..16], 0x00 otherwise
        uint8_t bad_range = static_cast<uint8_t>(((pad_val - 1) >> 8) | ((16 - pad_val) >> 8));

        // Always scan exactly 16 bytes of the last block
        const uint8_t *last_block = decrypted.data() + decrypted.size() - 16;
        volatile uint8_t bad_pad = 0;
        for (unsigned j = 0; j < 16; ++j)
        {
            // should_be_pad is 0xFF when byte j is in the padding region, 0x00
            // otherwise Padding region: positions where (j + pad_val >= 16), i.e. j >=
            // 16 - pad_val
            uint8_t should_be_pad = static_cast<uint8_t>((15 - j - pad_val) >> 8);
            bad_pad |= static_cast<uint8_t>(should_be_pad & (last_block[j] ^ pad_val));
        }

        uint8_t bad = static_cast<uint8_t>(bad_range | bad_pad);
        if (bad != 0)
        {
            secure_zero(decrypted.data(), decrypted.size());
            return Result::InvalidPadding;
        }

        size_t start = decrypted.size() - pad_val;
        plaintext.assign(decrypted.begin(), decrypted.begin() + static_cast<ptrdiff_t>(start));
        secure_zero(decrypted.data(), decrypted.size());
        return Result::Ok;
    }

    Result cbc_encrypt_pkcs7(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &iv_and_ciphertext)
    {
        auto iv = generate_iv();
        if (iv.empty())
            return Result::InternalError;

        std::vector<uint8_t> ct;
        auto result = cbc_encrypt_pkcs7(key, iv, plaintext, ct);
        if (result != Result::Ok)
            return result;

        iv_and_ciphertext.resize(16 + ct.size());
        std::memcpy(iv_and_ciphertext.data(), iv.data(), 16);
        std::memcpy(iv_and_ciphertext.data() + 16, ct.data(), ct.size());
        return Result::Ok;
    }

    Result cbc_decrypt_pkcs7(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv_and_ciphertext,
        std::vector<uint8_t> &plaintext)
    {
        if (iv_and_ciphertext.size() < 32)
            return Result::InvalidInputSize;

        std::vector<uint8_t> iv(iv_and_ciphertext.begin(), iv_and_ciphertext.begin() + 16);
        std::vector<uint8_t> ct(iv_and_ciphertext.begin() + 16, iv_and_ciphertext.end());
        return cbc_decrypt_pkcs7(key, iv, ct, plaintext);
    }

} // namespace tinyaes

// C API implementations
extern "C" int tinyaes_cbc_encrypt(
    const uint8_t *key,
    size_t key_len,
    const uint8_t iv[16],
    const uint8_t *plaintext,
    size_t plaintext_len,
    uint8_t *ciphertext,
    size_t ciphertext_len)
{
    if (!key || !iv || !plaintext || !ciphertext)
        return TINYAES_INVALID_INPUT_SIZE;
    if (ciphertext_len < plaintext_len)
        return TINYAES_INVALID_INPUT_SIZE;

    std::vector<uint8_t> k(key, key + key_len);
    std::vector<uint8_t> v(iv, iv + 16);
    std::vector<uint8_t> pt(plaintext, plaintext + plaintext_len);
    std::vector<uint8_t> ct;

    auto result = tinyaes::cbc_encrypt(k, v, pt, ct);
    tinyaes::secure_zero(k.data(), k.size());
    tinyaes::secure_zero(pt.data(), pt.size());

    if (result != tinyaes::Result::Ok)
        return static_cast<int>(result);

    std::memcpy(ciphertext, ct.data(), ct.size());
    return TINYAES_OK;
}

extern "C" int tinyaes_cbc_decrypt(
    const uint8_t *key,
    size_t key_len,
    const uint8_t iv[16],
    const uint8_t *ciphertext,
    size_t ciphertext_len,
    uint8_t *plaintext,
    size_t plaintext_len)
{
    if (!key || !iv || !ciphertext || !plaintext)
        return TINYAES_INVALID_INPUT_SIZE;
    if (plaintext_len < ciphertext_len)
        return TINYAES_INVALID_INPUT_SIZE;

    std::vector<uint8_t> k(key, key + key_len);
    std::vector<uint8_t> v(iv, iv + 16);
    std::vector<uint8_t> ct(ciphertext, ciphertext + ciphertext_len);
    std::vector<uint8_t> pt;

    auto result = tinyaes::cbc_decrypt(k, v, ct, pt);
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

extern "C" int tinyaes_cbc_encrypt_pkcs7(
    const uint8_t *key,
    size_t key_len,
    const uint8_t iv[16],
    const uint8_t *plaintext,
    size_t plaintext_len,
    uint8_t *ciphertext,
    size_t *ciphertext_len)
{
    if (!key || !iv || !plaintext || !ciphertext || !ciphertext_len)
        return TINYAES_INVALID_INPUT_SIZE;

    size_t padded_len = plaintext_len + (16 - (plaintext_len % 16));
    if (*ciphertext_len < padded_len)
    {
        *ciphertext_len = padded_len;
        return TINYAES_INVALID_INPUT_SIZE;
    }

    std::vector<uint8_t> k(key, key + key_len);
    std::vector<uint8_t> v(iv, iv + 16);
    std::vector<uint8_t> pt(plaintext, plaintext + plaintext_len);
    std::vector<uint8_t> ct;

    auto result = tinyaes::cbc_encrypt_pkcs7(k, v, pt, ct);
    tinyaes::secure_zero(k.data(), k.size());
    tinyaes::secure_zero(pt.data(), pt.size());

    if (result != tinyaes::Result::Ok)
        return static_cast<int>(result);

    std::memcpy(ciphertext, ct.data(), ct.size());
    *ciphertext_len = ct.size();
    return TINYAES_OK;
}

extern "C" int tinyaes_cbc_decrypt_pkcs7(
    const uint8_t *key,
    size_t key_len,
    const uint8_t iv[16],
    const uint8_t *ciphertext,
    size_t ciphertext_len,
    uint8_t *plaintext,
    size_t *plaintext_len)
{
    if (!key || !iv || !ciphertext || !plaintext || !plaintext_len)
        return TINYAES_INVALID_INPUT_SIZE;

    std::vector<uint8_t> k(key, key + key_len);
    std::vector<uint8_t> v(iv, iv + 16);
    std::vector<uint8_t> ct(ciphertext, ciphertext + ciphertext_len);
    std::vector<uint8_t> pt;

    auto result = tinyaes::cbc_decrypt_pkcs7(k, v, ct, pt);
    tinyaes::secure_zero(k.data(), k.size());

    if (result != tinyaes::Result::Ok)
        return static_cast<int>(result);

    if (*plaintext_len < pt.size())
    {
        *plaintext_len = pt.size();
        tinyaes::secure_zero(pt.data(), pt.size());
        return TINYAES_INVALID_INPUT_SIZE;
    }

    if (!pt.empty())
        std::memcpy(plaintext, pt.data(), pt.size());
    *plaintext_len = pt.size();
    tinyaes::secure_zero(pt.data(), pt.size());
    return TINYAES_OK;
}
