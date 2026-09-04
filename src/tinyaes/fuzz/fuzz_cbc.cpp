// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "internal/aes_impl.h"
#include "tinyaes/cbc.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Differential: verify portable encrypt_block matches dispatched for each block
static void diff_encrypt_block(const uint8_t *key, size_t key_len, const uint8_t block[16])
{
    int rounds = tinyaes::internal::aes_rounds(key_len);
    if (rounds == 0)
        return;

    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, key_len, rk);

    uint8_t out_portable[16], out_dispatch[16];
    tinyaes::internal::aes_encrypt_block_portable(rk, rounds, block, out_portable);
    tinyaes::internal::get_encrypt_block()(rk, rounds, block, out_dispatch);
    assert(std::memcmp(out_portable, out_dispatch, 16) == 0);
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

    if (size < key_len + 16 + 1)
        return 0;

    std::vector<uint8_t> key(data, data + key_len);
    std::vector<uint8_t> iv(data + key_len, data + key_len + 16);

    size_t remaining = size - key_len - 16;
    if (remaining == 0)
        return 0;

    std::vector<uint8_t> plaintext(data + key_len + 16, data + size);
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::cbc_encrypt_pkcs7(key, iv, plaintext, ct);
    if (result != tinyaes::Result::Ok)
        return 0;

    result = tinyaes::cbc_decrypt_pkcs7(key, iv, ct, pt);
    assert(result == tinyaes::Result::Ok);
    assert(pt == plaintext);

    // Differential: compare portable vs dispatched on a single block from the
    // input
    diff_encrypt_block(data, key_len, data + key_len);

    return 0;
}
