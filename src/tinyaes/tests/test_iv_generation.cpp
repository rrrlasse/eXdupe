// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "test_harness.h"
#include "tinyaes/common.h"

#include <cstring>

TEST(iv_generate_12_bytes)
{
    uint8_t iv[12] = {0};
    int result = tinyaes::generate_iv(iv, 12);
    ASSERT_TRUE(result == 0);

    // Check that it's not all zeros (extremely unlikely for CSPRNG)
    uint8_t zeros[12] = {0};
    ASSERT_TRUE(std::memcmp(iv, zeros, 12) != 0);
}

TEST(iv_generate_16_bytes)
{
    uint8_t iv[16] = {0};
    int result = tinyaes::generate_iv(iv, 16);
    ASSERT_TRUE(result == 0);

    uint8_t zeros[16] = {0};
    ASSERT_TRUE(std::memcmp(iv, zeros, 16) != 0);
}

TEST(iv_generate_uniqueness)
{
    // Two consecutive IV generations should produce different values
    uint8_t iv1[16], iv2[16];
    tinyaes::generate_iv(iv1, 16);
    tinyaes::generate_iv(iv2, 16);
    ASSERT_TRUE(std::memcmp(iv1, iv2, 16) != 0);
}

TEST(iv_generate_null_rejected)
{
    int result = tinyaes::generate_iv(nullptr, 16);
    ASSERT_TRUE(result == -1);
}

TEST(iv_generate_zero_len_rejected)
{
    uint8_t iv[1];
    int result = tinyaes::generate_iv(iv, 0);
    ASSERT_TRUE(result == -1);
}
