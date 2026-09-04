// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "internal/ghash.h"

#include <cassert>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Need at least 16 bytes for the hash subkey H
    if (size < 16)
        return 0;

    const uint8_t *H = data;
    const uint8_t *payload = data + 16;
    size_t payload_len = size - 16;

    uint8_t Y_portable[16] = {0};
    uint8_t Y_dispatch[16] = {0};

    tinyaes::internal::ghash_portable(H, payload, payload_len, Y_portable);
    tinyaes::internal::get_ghash()(H, payload, payload_len, Y_dispatch);

    assert(std::memcmp(Y_portable, Y_dispatch, 16) == 0);

    return 0;
}
