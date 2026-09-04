// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "internal/aes_impl.h"
#include "tinyaes/ecb.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Differential: compare dispatched encrypt_block against portable
static void diff_encrypt_block(const uint8_t *key, size_t key_len, const uint8_t *plaintext, size_t pt_len)
{
    int rounds = tinyaes::internal::aes_rounds(key_len);
    if (rounds == 0)
        return;

    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS];

    // Portable key expansion + encrypt
    tinyaes::internal::aes_key_expand_portable(key, key_len, rk);

    for (size_t off = 0; off + 16 <= pt_len; off += 16)
    {
        uint8_t out_portable[16], out_dispatch[16];
        tinyaes::internal::aes_encrypt_block_portable(rk, rounds, plaintext + off, out_portable);
        tinyaes::internal::get_encrypt_block()(rk, rounds, plaintext + off, out_dispatch);
        assert(std::memcmp(out_portable, out_dispatch, 16) == 0);

        // Also verify decrypt roundtrip
        uint8_t dec_portable[16], dec_dispatch[16];
        tinyaes::internal::aes_decrypt_block_portable(rk, rounds, out_portable, dec_portable);
        tinyaes::internal::get_decrypt_block()(rk, rounds, out_dispatch, dec_dispatch);
        assert(std::memcmp(dec_portable, dec_dispatch, 16) == 0);
        assert(std::memcmp(dec_portable, plaintext + off, 16) == 0);
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

    if (size < key_len + 16)
        return 0;

    std::vector<uint8_t> key(data, data + key_len);
    size_t pt_len = ((size - key_len) / 16) * 16;
    if (pt_len == 0)
        return 0;

    std::vector<uint8_t> plaintext(data + key_len, data + key_len + pt_len);
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::ecb_encrypt(key, plaintext, ct);
    if (result != tinyaes::Result::Ok)
        return 0;

    result = tinyaes::ecb_decrypt(key, ct, pt);
    assert(result == tinyaes::Result::Ok);
    assert(pt == plaintext);

    // Differential test: portable vs dispatched
    diff_encrypt_block(data, key_len, data + key_len, pt_len);

    return 0;
}
