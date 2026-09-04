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

#include "tinyaes/gcm.h"

#include "internal/aes_impl.h"
#include "internal/endian.h"
#include "internal/ghash.h"

#include <cstdint>
#include <cstring>

namespace tinyaes
{

    // Compute J0 from IV per NIST SP 800-38D Section 7.1
    static void
        compute_j0(const uint8_t *iv, size_t iv_len, const uint8_t H[16], internal::ghash_fn ghash, uint8_t J0[16])
    {
        if (iv_len == 12)
        {
            // If len(IV) = 96 bits: J0 = IV || 0^31 || 1
            std::memcpy(J0, iv, 12);
            J0[12] = 0;
            J0[13] = 0;
            J0[14] = 0;
            J0[15] = 1;
        }
        else
        {
            // J0 = GHASH_H(IV || 0^s || len(IV)_64)
            // where s = 128*ceil(len(IV)/128) - len(IV) + 64
            std::memset(J0, 0, 16);

            // GHASH the IV data (handles padding internally)
            ghash(H, iv, iv_len, J0);

            // Pad to 16-byte boundary, then add 8 zero bytes + 8-byte big-endian bit
            // length
            uint8_t len_block[16] = {0};
            uint64_t iv_bits = static_cast<uint64_t>(iv_len) * 8;
            internal::store_be64(len_block + 8, iv_bits);
            ghash(H, len_block, 16, J0);
        }
    }

    // Compute GCM tag per NIST SP 800-38D
    static void compute_tag(
        const uint32_t *rk,
        int rounds,
        const uint8_t H[16],
        const uint8_t J0[16],
        const uint8_t *aad,
        size_t aad_len,
        const uint8_t *ciphertext,
        size_t ciphertext_len,
        internal::ghash_fn ghash,
        internal::encrypt_block_fn encrypt_block,
        uint8_t tag[16])
    {
        uint8_t S[16] = {0};

        // GHASH AAD
        if (aad_len > 0)
        {
            ghash(H, aad, aad_len, S);
        }

        // GHASH ciphertext
        if (ciphertext_len > 0)
        {
            ghash(H, ciphertext, ciphertext_len, S);
        }

        // Append length block: [len(A)_64 || len(C)_64] in bits
        uint8_t len_block[16];
        internal::store_be64(len_block, static_cast<uint64_t>(aad_len) * 8);
        internal::store_be64(len_block + 8, static_cast<uint64_t>(ciphertext_len) * 8);
        ghash(H, len_block, 16, S);

        // T = S XOR E_K(J0)
        uint8_t E_J0[16];
        encrypt_block(rk, rounds, J0, E_J0);
        for (int i = 0; i < 16; ++i)
        {
            tag[i] = S[i] ^ E_J0[i];
        }
    }

    Result gcm_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &aad,
        const std::vector<uint8_t> &plaintext,
        std::vector<uint8_t> &ciphertext,
        std::vector<uint8_t> &tag)
    {
        int rounds = internal::aes_rounds(key.size());
        if (rounds == 0)
            return Result::InvalidKeySize;
        if (iv.empty())
            return Result::InvalidIVSize;
        // NIST SP 800-38D Section 5.2.1.1: len(P) <= 2^39 - 256 bits = (2^36 - 32)
        // bytes
        if (plaintext.size() > ((UINT64_C(1) << 36) - 32))
            return Result::InvalidInputSize;
        // NIST SP 800-38D Section 5.2.1.1: len(A) <= 2^64 - 1 bits
        // This exceeds size_t range on all platforms, so no runtime check is needed.

        uint32_t rk[internal::AES_MAX_RK_WORDS];
        auto key_expand = internal::get_key_expand();
        key_expand(key.data(), key.size(), rk);

        auto encrypt_block = internal::get_encrypt_block();
        auto ghash = internal::get_ghash();

        // Compute H = E_K(0^128)
        uint8_t H[16] = {0};
        uint8_t zero_block[16] = {0};
        encrypt_block(rk, rounds, zero_block, H);

        // Compute J0
        uint8_t J0[16];
        compute_j0(iv.data(), iv.size(), H, ghash, J0);

        // Encrypt plaintext using CTR mode starting from inc32(J0)
        ciphertext.resize(plaintext.size());

        if (!plaintext.empty())
        {
            uint8_t ctr[16];
            std::memcpy(ctr, J0, 16);
            internal::increment_be32(ctr);

            size_t full_blocks = plaintext.size() / 16;
            size_t remainder = plaintext.size() % 16;

            if (full_blocks > 0)
            {
                auto ctr_pipeline = internal::get_ctr_pipeline();
                ctr_pipeline(rk, rounds, plaintext.data(), ciphertext.data(), full_blocks, ctr);
            }

            if (remainder > 0)
            {
                uint8_t keystream[16];
                encrypt_block(rk, rounds, ctr, keystream);
                size_t offset = full_blocks * 16;
                for (size_t i = 0; i < remainder; ++i)
                {
                    ciphertext[offset + i] = plaintext[offset + i] ^ keystream[i];
                }
                secure_zero(keystream, sizeof(keystream));
            }
        }

        // Compute authentication tag
        tag.resize(16);
        compute_tag(
            rk,
            rounds,
            H,
            J0,
            aad.data(),
            aad.size(),
            ciphertext.data(),
            ciphertext.size(),
            ghash,
            encrypt_block,
            tag.data());

        secure_zero(rk, sizeof(rk));
        secure_zero(H, sizeof(H));
        secure_zero(J0, sizeof(J0));
        return Result::Ok;
    }

    Result gcm_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &iv,
        const std::vector<uint8_t> &aad,
        const std::vector<uint8_t> &ciphertext,
        const std::vector<uint8_t> &tag,
        std::vector<uint8_t> &plaintext)
    {
        int rounds = internal::aes_rounds(key.size());
        if (rounds == 0)
            return Result::InvalidKeySize;
        if (iv.empty())
            return Result::InvalidIVSize;
        if (tag.size() != 16)
            return Result::InvalidInputSize;
        // NIST SP 800-38D Section 5.2.1.1: len(C) <= 2^39 - 256 bits = (2^36 - 32)
        // bytes
        if (ciphertext.size() > ((UINT64_C(1) << 36) - 32))
            return Result::InvalidInputSize;
        // NIST SP 800-38D Section 5.2.1.1: len(A) <= 2^64 - 1 bits
        // This exceeds size_t range on all platforms, so no runtime check is needed.

        uint32_t rk[internal::AES_MAX_RK_WORDS];
        auto key_expand = internal::get_key_expand();
        key_expand(key.data(), key.size(), rk);

        auto encrypt_block = internal::get_encrypt_block();
        auto ghash = internal::get_ghash();

        // Compute H = E_K(0^128)
        uint8_t H[16] = {0};
        uint8_t zero_block[16] = {0};
        encrypt_block(rk, rounds, zero_block, H);

        // Compute J0
        uint8_t J0[16];
        compute_j0(iv.data(), iv.size(), H, ghash, J0);

        // Verify tag BEFORE decrypting (decrypt into temp buffer)
        uint8_t computed_tag[16];
        compute_tag(
            rk,
            rounds,
            H,
            J0,
            aad.data(),
            aad.size(),
            ciphertext.data(),
            ciphertext.size(),
            ghash,
            encrypt_block,
            computed_tag);

        if (!constant_time_equal(computed_tag, tag.data(), 16))
        {
            secure_zero(rk, sizeof(rk));
            secure_zero(H, sizeof(H));
            secure_zero(J0, sizeof(J0));
            secure_zero(computed_tag, sizeof(computed_tag));
            return Result::AuthenticationFailed;
        }

        // Tag verified — decrypt ciphertext using CTR mode
        plaintext.resize(ciphertext.size());

        if (!ciphertext.empty())
        {
            uint8_t ctr[16];
            std::memcpy(ctr, J0, 16);
            internal::increment_be32(ctr);

            size_t full_blocks = ciphertext.size() / 16;
            size_t remainder = ciphertext.size() % 16;

            if (full_blocks > 0)
            {
                auto ctr_pipeline = internal::get_ctr_pipeline();
                ctr_pipeline(rk, rounds, ciphertext.data(), plaintext.data(), full_blocks, ctr);
            }

            if (remainder > 0)
            {
                uint8_t keystream[16];
                encrypt_block(rk, rounds, ctr, keystream);
                size_t offset = full_blocks * 16;
                for (size_t i = 0; i < remainder; ++i)
                {
                    plaintext[offset + i] = ciphertext[offset + i] ^ keystream[i];
                }
                secure_zero(keystream, sizeof(keystream));
            }
        }

        secure_zero(rk, sizeof(rk));
        secure_zero(H, sizeof(H));
        secure_zero(J0, sizeof(J0));
        secure_zero(computed_tag, sizeof(computed_tag));
        return Result::Ok;
    }

    Result gcm_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &nonce,
        const std::vector<uint8_t> &plaintext,
        const std::vector<uint8_t> &aad,
        std::vector<uint8_t> &ciphertext_and_tag)
    {
        std::vector<uint8_t> ct, tag;
        auto result = gcm_encrypt(key, nonce, aad, plaintext, ct, tag);
        if (result != Result::Ok)
            return result;

        ciphertext_and_tag.resize(ct.size() + 16);
        if (!ct.empty())
            std::memcpy(ciphertext_and_tag.data(), ct.data(), ct.size());
        std::memcpy(ciphertext_and_tag.data() + ct.size(), tag.data(), 16);
        return Result::Ok;
    }

    Result gcm_encrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &plaintext,
        const std::vector<uint8_t> &aad,
        std::vector<uint8_t> &nonce_ciphertext_tag)
    {
        auto nonce = generate_nonce();
        if (nonce.empty())
            return Result::InternalError;

        std::vector<uint8_t> ct_tag;
        auto result = gcm_encrypt(key, nonce, plaintext, aad, ct_tag);
        if (result != Result::Ok)
            return result;

        nonce_ciphertext_tag.resize(12 + ct_tag.size());
        std::memcpy(nonce_ciphertext_tag.data(), nonce.data(), 12);
        std::memcpy(nonce_ciphertext_tag.data() + 12, ct_tag.data(), ct_tag.size());
        return Result::Ok;
    }

    Result gcm_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &nonce,
        const std::vector<uint8_t> &ciphertext_and_tag,
        const std::vector<uint8_t> &aad,
        std::vector<uint8_t> &plaintext)
    {
        if (ciphertext_and_tag.size() < 16)
            return Result::InvalidInputSize;

        size_t ct_len = ciphertext_and_tag.size() - 16;
        std::vector<uint8_t> ct(
            ciphertext_and_tag.begin(), ciphertext_and_tag.begin() + static_cast<ptrdiff_t>(ct_len));
        std::vector<uint8_t> tag(ciphertext_and_tag.end() - 16, ciphertext_and_tag.end());
        return gcm_decrypt(key, nonce, aad, ct, tag, plaintext);
    }

    Result gcm_decrypt(
        const std::vector<uint8_t> &key,
        const std::vector<uint8_t> &nonce_ciphertext_tag,
        const std::vector<uint8_t> &aad,
        std::vector<uint8_t> &plaintext)
    {
        if (nonce_ciphertext_tag.size() < 28) // 12 nonce + 16 tag minimum
            return Result::InvalidInputSize;

        std::vector<uint8_t> nonce(nonce_ciphertext_tag.begin(), nonce_ciphertext_tag.begin() + 12);
        std::vector<uint8_t> ct_tag(nonce_ciphertext_tag.begin() + 12, nonce_ciphertext_tag.end());
        return gcm_decrypt(key, nonce, ct_tag, aad, plaintext);
    }

} // namespace tinyaes

extern "C" int tinyaes_gcm_encrypt(
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
    uint8_t tag[16])
{
    if (!key || !iv || !tag)
        return TINYAES_INVALID_INPUT_SIZE;
    if (plaintext_len > 0 && (!plaintext || !ciphertext))
        return TINYAES_INVALID_INPUT_SIZE;
    if (ciphertext_len < plaintext_len)
        return TINYAES_INVALID_INPUT_SIZE;

    std::vector<uint8_t> k(key, key + key_len);
    std::vector<uint8_t> v(iv, iv + iv_len);
    std::vector<uint8_t> a;
    if (aad && aad_len > 0)
        a.assign(aad, aad + aad_len);
    std::vector<uint8_t> pt;
    if (plaintext && plaintext_len > 0)
        pt.assign(plaintext, plaintext + plaintext_len);
    std::vector<uint8_t> ct, t;

    auto result = tinyaes::gcm_encrypt(k, v, a, pt, ct, t);
    tinyaes::secure_zero(k.data(), k.size());
    tinyaes::secure_zero(pt.data(), pt.size());

    if (result != tinyaes::Result::Ok)
        return static_cast<int>(result);

    if (!ct.empty())
        std::memcpy(ciphertext, ct.data(), ct.size());
    std::memcpy(tag, t.data(), 16);
    return TINYAES_OK;
}

extern "C" int tinyaes_gcm_decrypt(
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
    const uint8_t tag[16])
{
    if (!key || !iv || !tag)
        return TINYAES_INVALID_INPUT_SIZE;
    if (ciphertext_len > 0 && (!ciphertext || !plaintext))
        return TINYAES_INVALID_INPUT_SIZE;
    if (plaintext_len < ciphertext_len)
        return TINYAES_INVALID_INPUT_SIZE;

    std::vector<uint8_t> k(key, key + key_len);
    std::vector<uint8_t> v(iv, iv + iv_len);
    std::vector<uint8_t> a;
    if (aad && aad_len > 0)
        a.assign(aad, aad + aad_len);
    std::vector<uint8_t> ct;
    if (ciphertext && ciphertext_len > 0)
        ct.assign(ciphertext, ciphertext + ciphertext_len);
    std::vector<uint8_t> t(tag, tag + 16);
    std::vector<uint8_t> pt;

    auto result = tinyaes::gcm_decrypt(k, v, a, ct, t, pt);
    tinyaes::secure_zero(k.data(), k.size());

    if (result != tinyaes::Result::Ok)
    {
        tinyaes::secure_zero(pt.data(), pt.size());
        return static_cast<int>(result);
    }

    if (!pt.empty())
        std::memcpy(plaintext, pt.data(), pt.size());
    tinyaes::secure_zero(pt.data(), pt.size());
    return TINYAES_OK;
}
