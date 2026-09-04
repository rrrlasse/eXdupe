// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "test_harness.h"
#include "tinyaes/ecb.h"
#include "vectors/aes_ecb_vectors.inl"

#define VEC(arr) std::vector<uint8_t>(arr, arr + sizeof(arr))

TEST(ecb_aes128_encrypt)
{
    std::vector<uint8_t> ct;
    auto result = tinyaes::ecb_encrypt(VEC(ecb_128_key), VEC(ecb_128_plain), ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(ecb_128_cipher));
}

TEST(ecb_aes128_decrypt)
{
    std::vector<uint8_t> pt;
    auto result = tinyaes::ecb_decrypt(VEC(ecb_128_key), VEC(ecb_128_cipher), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(ecb_128_plain));
}

TEST(ecb_aes192_encrypt)
{
    std::vector<uint8_t> ct;
    auto result = tinyaes::ecb_encrypt(VEC(ecb_192_key), VEC(ecb_192_plain), ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(ecb_192_cipher));
}

TEST(ecb_aes192_decrypt)
{
    std::vector<uint8_t> pt;
    auto result = tinyaes::ecb_decrypt(VEC(ecb_192_key), VEC(ecb_192_cipher), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(ecb_192_plain));
}

TEST(ecb_aes256_encrypt)
{
    std::vector<uint8_t> ct;
    auto result = tinyaes::ecb_encrypt(VEC(ecb_256_key), VEC(ecb_256_plain), ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(ecb_256_cipher));
}

TEST(ecb_aes256_decrypt)
{
    std::vector<uint8_t> pt;
    auto result = tinyaes::ecb_decrypt(VEC(ecb_256_key), VEC(ecb_256_cipher), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(ecb_256_plain));
}

TEST(ecb_aes128_multi_block)
{
    std::vector<uint8_t> ct;
    auto result = tinyaes::ecb_encrypt(VEC(ecb_128_multi_key), VEC(ecb_128_multi_plain), ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(ecb_128_multi_cipher));
}

TEST(ecb_aes128_multi_block_decrypt)
{
    std::vector<uint8_t> pt;
    auto result = tinyaes::ecb_decrypt(VEC(ecb_128_multi_key), VEC(ecb_128_multi_cipher), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(ecb_128_multi_plain));
}

TEST(ecb_invalid_key_size)
{
    std::vector<uint8_t> key(15, 0), pt(16, 0), ct;
    ASSERT_TRUE(tinyaes::ecb_encrypt(key, pt, ct) == tinyaes::Result::InvalidKeySize);
}

TEST(ecb_non_block_aligned)
{
    std::vector<uint8_t> key(16, 0), pt(17, 0), ct;
    ASSERT_TRUE(tinyaes::ecb_encrypt(key, pt, ct) == tinyaes::Result::InvalidInputSize);
}

TEST(ecb_empty_input)
{
    std::vector<uint8_t> key(16, 0), pt, ct;
    ASSERT_TRUE(tinyaes::ecb_encrypt(key, pt, ct) == tinyaes::Result::InvalidInputSize);
}

TEST(ecb_8_block_roundtrip)
{
    std::vector<uint8_t> key(32, 0xAB); // AES-256
    std::vector<uint8_t> plaintext(128, 0x55); // 8 blocks
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::ecb_encrypt(key, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 128);

    result = tinyaes::ecb_decrypt(key, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(ecb_16_block_roundtrip)
{
    std::vector<uint8_t> key(24, 0xCD); // AES-192
    std::vector<uint8_t> plaintext(256, 0x77); // 16 blocks
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::ecb_encrypt(key, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 256);

    result = tinyaes::ecb_decrypt(key, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

#undef VEC
