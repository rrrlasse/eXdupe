// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "internal/aes_impl.h"
#include "internal/ghash.h"
#include "tinyaes/gcm.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Differential: compare portable GHASH against dispatched
static void diff_ghash(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len)
{
    int rounds = tinyaes::internal::aes_rounds(key_len);
    if (rounds == 0)
        return;

    // Compute H = E_K(0^128)
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, key_len, rk);

    uint8_t H[16] = {0};
    uint8_t zero[16] = {0};
    tinyaes::internal::aes_encrypt_block_portable(rk, rounds, zero, H);

    uint8_t Y_portable[16] = {0};
    uint8_t Y_dispatch[16] = {0};

    tinyaes::internal::ghash_portable(H, data, data_len, Y_portable);
    tinyaes::internal::get_ghash()(H, data, data_len, Y_dispatch);

    assert(std::memcmp(Y_portable, Y_dispatch, 16) == 0);
}

// Tamper test: flip a bit in ciphertext/tag/aad and verify auth failure
static void tamper_test(
    const std::vector<uint8_t> &key,
    const std::vector<uint8_t> &iv,
    const std::vector<uint8_t> &aad,
    const std::vector<uint8_t> &ct,
    const std::vector<uint8_t> &tag,
    uint8_t tamper_byte)
{
    std::vector<uint8_t> pt;

    // Tamper ciphertext (if non-empty)
    if (!ct.empty())
    {
        std::vector<uint8_t> bad_ct = ct;
        bad_ct[tamper_byte % bad_ct.size()] ^= 0x01;
        auto result = tinyaes::gcm_decrypt(key, iv, aad, bad_ct, tag, pt);
        assert(result == tinyaes::Result::AuthenticationFailed);
        (void)result;
    }

    // Tamper tag
    {
        std::vector<uint8_t> bad_tag = tag;
        bad_tag[tamper_byte % 16] ^= 0x01;
        auto result = tinyaes::gcm_decrypt(key, iv, aad, ct, bad_tag, pt);
        assert(result == tinyaes::Result::AuthenticationFailed);
        (void)result;
    }

    // Tamper AAD (if non-empty)
    if (!aad.empty())
    {
        std::vector<uint8_t> bad_aad = aad;
        bad_aad[tamper_byte % bad_aad.size()] ^= 0x01;
        auto result = tinyaes::gcm_decrypt(key, iv, bad_aad, ct, tag, pt);
        assert(result == tinyaes::Result::AuthenticationFailed);
        (void)result;
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;

    // First byte selects key size: 16, 24, or 32
    static const size_t key_sizes[] = {16, 24, 32};
    size_t key_len = key_sizes[data[0] % 3];
    data++;
    size--;

    // Next byte selects IV length from common sizes
    static const size_t iv_sizes[] = {1, 8, 12, 16, 20, 32, 60};
    if (size < 1)
        return 0;
    size_t iv_len = iv_sizes[data[0] % 7];
    data++;
    size--;

    // key_len bytes key + iv_len bytes IV + 1 byte AAD length selector
    if (size < key_len + iv_len + 1)
        return 0;

    std::vector<uint8_t> key(data, data + key_len);
    std::vector<uint8_t> iv(data + key_len, data + key_len + iv_len);
    size_t header = key_len + iv_len + 1;
    uint8_t aad_selector = data[key_len + iv_len];
    size_t remaining = size - header;
    size_t aad_len = aad_selector % (remaining + 1);
    if (aad_len > remaining)
        aad_len = 0;

    std::vector<uint8_t> aad(data + header, data + header + aad_len);
    std::vector<uint8_t> plaintext(data + header + aad_len, data + size);

    std::vector<uint8_t> ct, tag, pt;

    auto result = tinyaes::gcm_encrypt(key, iv, aad, plaintext, ct, tag);
    if (result != tinyaes::Result::Ok)
        return 0;

    result = tinyaes::gcm_decrypt(key, iv, aad, ct, tag, pt);
    assert(result == tinyaes::Result::Ok);
    assert(pt == plaintext);

    // Differential: portable GHASH vs dispatched
    size_t total_data = aad_len + plaintext.size();
    if (total_data > 0)
    {
        diff_ghash(data, key_len, data + header, total_data);
    }

    // Tamper test: verify authentication catches single-bit corruption
    tamper_test(key, iv, aad, ct, tag, aad_selector);

    return 0;
}
