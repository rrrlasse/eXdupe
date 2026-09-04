// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "internal/aes_impl.h"

#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;

    // First byte selects key size: 16, 24, or 32
    static const size_t key_sizes[] = {16, 24, 32};
    size_t key_len = key_sizes[data[0] % 3];
    data++;
    size--;

    if (size < key_len)
        return 0;

    uint32_t rk_portable[tinyaes::internal::AES_MAX_RK_WORDS];
    uint32_t rk_dispatch[tinyaes::internal::AES_MAX_RK_WORDS];

    tinyaes::internal::aes_key_expand_portable(data, key_len, rk_portable);
    tinyaes::internal::get_key_expand()(data, key_len, rk_dispatch);

    // Encrypt a known block with both expanded keys and compare
    int rounds = tinyaes::internal::aes_rounds(key_len);
    uint8_t test_block[16] = {
        0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d, 0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34};
    uint8_t out_p[16], out_d[16];
    tinyaes::internal::aes_encrypt_block_portable(rk_portable, rounds, test_block, out_p);
    tinyaes::internal::get_encrypt_block()(rk_dispatch, rounds, test_block, out_d);
    if (std::memcmp(out_p, out_d, 16) != 0)
        __builtin_trap();

    return 0;
}
