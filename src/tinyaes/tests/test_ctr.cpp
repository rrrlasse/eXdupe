// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "test_harness.h"
#include "tinyaes/ctr.h"
#include "vectors/aes_ctr_vectors.inl"

#define VEC(arr) std::vector<uint8_t>(arr, arr + sizeof(arr))

TEST(ctr_aes128_encrypt)
{
    std::vector<uint8_t> ct;
    auto result = tinyaes::ctr_crypt(VEC(ctr_128_key), VEC(ctr_128_iv), VEC(ctr_128_plain), ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(ctr_128_cipher));
}

TEST(ctr_aes128_decrypt)
{
    std::vector<uint8_t> pt;
    auto result = tinyaes::ctr_crypt(VEC(ctr_128_key), VEC(ctr_128_iv), VEC(ctr_128_cipher), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(ctr_128_plain));
}

TEST(ctr_aes192_encrypt)
{
    std::vector<uint8_t> ct;
    auto result = tinyaes::ctr_crypt(VEC(ctr_192_key), VEC(ctr_192_iv), VEC(ctr_192_plain), ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(ctr_192_cipher));
}

TEST(ctr_aes256_encrypt)
{
    std::vector<uint8_t> ct;
    auto result = tinyaes::ctr_crypt(VEC(ctr_256_key), VEC(ctr_256_iv), VEC(ctr_256_plain), ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(ctr_256_cipher));
}

TEST(ctr_partial_block)
{
    // Encrypt 7 bytes with CTR — should handle partial block
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> iv(16, 0x00);
    std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::ctr_crypt(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 7);

    // Decrypt should recover original
    result = tinyaes::ctr_crypt(key, iv, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(ctr_roundtrip_multi_block)
{
    std::vector<uint8_t> key(32, 0xAB);
    std::vector<uint8_t> iv(16, 0x01);
    std::vector<uint8_t> plaintext(100, 0x55); // 6 full blocks + 4 remainder
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::ctr_crypt(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 100);

    result = tinyaes::ctr_crypt(key, iv, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(ctr_5_block_roundtrip)
{
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> iv(16, 0x01);
    std::vector<uint8_t> plaintext(80,
                                   0x55); // 5 blocks: pipeline loop + 1 remainder
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::ctr_crypt(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 80);

    result = tinyaes::ctr_crypt(key, iv, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(ctr_8_block_roundtrip)
{
    std::vector<uint8_t> key(32, 0xAB); // AES-256
    std::vector<uint8_t> iv(16, 0x02);
    std::vector<uint8_t> plaintext(128, 0x77); // 8 blocks: two 4-block pipeline iterations
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::ctr_crypt(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 128);

    result = tinyaes::ctr_crypt(key, iv, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(ctr_9_block_roundtrip)
{
    std::vector<uint8_t> key(24, 0xCD); // AES-192
    std::vector<uint8_t> iv(16, 0x03);
    std::vector<uint8_t> plaintext(144, 0x33); // 9 blocks: two pipeline iterations + 1 remainder
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::ctr_crypt(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 144);

    result = tinyaes::ctr_crypt(key, iv, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(ctr_zero_length_plaintext)
{
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> iv(16, 0x00);
    std::vector<uint8_t> plaintext;
    std::vector<uint8_t> ct;

    auto result = tinyaes::ctr_crypt(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::InvalidInputSize);
}

TEST(ctr_invalid_nonce_size)
{
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> nonce(10, 0x00); // wrong size
    std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03};
    std::vector<uint8_t> ct;

    auto result = tinyaes::ctr_encrypt(key, nonce, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::InvalidNonceSize);
}

TEST(ctr_nonce_encrypt_decrypt_roundtrip)
{
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> nonce(12, 0x01);
    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::ctr_encrypt(key, nonce, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 5);

    result = tinyaes::ctr_decrypt(key, nonce, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(ctr_auto_nonce_roundtrip)
{
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    std::vector<uint8_t> nonce_ct, pt;

    auto result = tinyaes::ctr_encrypt(key, plaintext, nonce_ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(nonce_ct.size() == 17); // 12 nonce + 5 ciphertext

    result = tinyaes::ctr_decrypt(key, nonce_ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(ctr_invalid_key_size)
{
    std::vector<uint8_t> key(15, 0x42); // invalid: not 16/24/32
    std::vector<uint8_t> iv(16, 0x00);
    std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03};
    std::vector<uint8_t> ct;

    auto result = tinyaes::ctr_crypt(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::InvalidKeySize);
}

#undef VEC
