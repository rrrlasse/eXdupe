// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "test_harness.h"
#include "tinyaes/cbc.h"

TEST(pkcs7_full_block_padding)
{
    // When input is exactly block-aligned, a full padding block (16 bytes of
    // 0x10) is added
    std::vector<uint8_t> key(16, 0xAA);
    std::vector<uint8_t> iv(16, 0x00);
    std::vector<uint8_t> plaintext(16, 0x42); // Exactly one block
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::cbc_encrypt_pkcs7(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 32); // 16 data + 16 padding

    result = tinyaes::cbc_decrypt_pkcs7(key, iv, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(pkcs7_single_byte)
{
    std::vector<uint8_t> key(16, 0xBB);
    std::vector<uint8_t> iv(16, 0x00);
    std::vector<uint8_t> plaintext = {0xFF};
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::cbc_encrypt_pkcs7(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 16); // 1 + 15 padding

    result = tinyaes::cbc_decrypt_pkcs7(key, iv, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(pkcs7_empty_plaintext)
{
    std::vector<uint8_t> key(16, 0xCC);
    std::vector<uint8_t> iv(16, 0x00);
    std::vector<uint8_t> plaintext; // Empty
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::cbc_encrypt_pkcs7(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 16); // Full block of padding (0x10)

    result = tinyaes::cbc_decrypt_pkcs7(key, iv, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(pkcs7_15_byte_plaintext)
{
    std::vector<uint8_t> key(16, 0xDD);
    std::vector<uint8_t> iv(16, 0x00);
    std::vector<uint8_t> plaintext(15, 0x42);
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::cbc_encrypt_pkcs7(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 16); // 15 + 1 padding byte

    result = tinyaes::cbc_decrypt_pkcs7(key, iv, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(pkcs7_invalid_padding_rejected)
{
    // Construct ciphertext that decrypts to invalid padding
    std::vector<uint8_t> key(16, 0xEE);
    std::vector<uint8_t> iv(16, 0x00);

    // Encrypt known plaintext, then corrupt last byte of ciphertext
    std::vector<uint8_t> plaintext(16, 0x42);
    std::vector<uint8_t> ct, pt;

    auto result = tinyaes::cbc_encrypt_pkcs7(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);

    // Flip a bit in the last block (padding block)
    ct.back() ^= 0x01;

    result = tinyaes::cbc_decrypt_pkcs7(key, iv, ct, pt);
    ASSERT_TRUE(result == tinyaes::Result::InvalidPadding);
}

TEST(pkcs7_multi_position_corruption)
{
    // Encrypt a known 16-byte plaintext, then corrupt each of the 16 positions
    // in the padding block and verify InvalidPadding for each
    std::vector<uint8_t> key(16, 0xEE);
    std::vector<uint8_t> iv(16, 0x00);
    std::vector<uint8_t> plaintext(16, 0x42);
    std::vector<uint8_t> ct_orig, pt;

    auto result = tinyaes::cbc_encrypt_pkcs7(key, iv, plaintext, ct_orig);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct_orig.size() == 32); // 16 data + 16 padding block

    for (size_t i = 0; i < 16; ++i)
    {
        std::vector<uint8_t> ct = ct_orig;
        ct[16 + i] ^= 0x01; // corrupt position i of the padding block
        result = tinyaes::cbc_decrypt_pkcs7(key, iv, ct, pt);
        ASSERT_TRUE(result == tinyaes::Result::InvalidPadding);
    }
}
