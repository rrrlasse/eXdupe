// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "internal/aes_impl.h"
#include "tinyaes/ctr.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Differential: run CTR pipeline via portable vs dispatched and compare
static void
    diff_ctr_pipeline(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, const uint8_t iv[16])
{
    int rounds = tinyaes::internal::aes_rounds(key_len);
    if (rounds == 0)
        return;

    size_t blocks = data_len / 16;
    if (blocks == 0)
        return;

    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, key_len, rk);

    std::vector<uint8_t> out_portable(blocks * 16);
    std::vector<uint8_t> out_dispatch(blocks * 16);
    uint8_t ctr_p[16], ctr_d[16];
    std::memcpy(ctr_p, iv, 16);
    std::memcpy(ctr_d, iv, 16);

    tinyaes::internal::aes_ctr_pipeline_portable(rk, rounds, data, out_portable.data(), blocks, ctr_p);
    tinyaes::internal::get_ctr_pipeline()(rk, rounds, data, out_dispatch.data(), blocks, ctr_d);

    assert(out_portable == out_dispatch);
    assert(std::memcmp(ctr_p, ctr_d, 16) == 0);
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
    std::vector<uint8_t> plaintext(data + key_len + 16, data + size);
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::ctr_crypt(key, iv, plaintext, ct);
    if (result != tinyaes::Result::Ok)
        return 0;

    result = tinyaes::ctr_crypt(key, iv, ct, pt);
    assert(result == tinyaes::Result::Ok);
    assert(pt == plaintext);

    // Differential: portable vs dispatched CTR pipeline
    diff_ctr_pipeline(data, key_len, data + key_len + 16, size - key_len - 16, data + key_len);

    return 0;
}
