// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "test_harness.h"
#include "tinyaes/cbc.h"
#include "tinyaes/common.h"

TEST(constant_time_equal_identical)
{
    uint8_t a[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    uint8_t b[16];
    std::memcpy(b, a, 16);
    ASSERT_TRUE(tinyaes::constant_time_equal(a, b, 16));
}

TEST(constant_time_equal_different)
{
    uint8_t a[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    uint8_t b[16];
    std::memcpy(b, a, 16);
    b[0] ^= 0xFF;
    ASSERT_TRUE(!tinyaes::constant_time_equal(a, b, 16));
}

TEST(constant_time_equal_last_byte)
{
    uint8_t a[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    uint8_t b[16];
    std::memcpy(b, a, 16);
    b[15] ^= 0x01;
    ASSERT_TRUE(!tinyaes::constant_time_equal(a, b, 16));
}

TEST(constant_time_equal_empty)
{
    uint8_t a = 0;
    uint8_t b = 0;
    ASSERT_TRUE(tinyaes::constant_time_equal(&a, &b, 0));
}

TEST(constant_time_equal_vector)
{
    std::vector<uint8_t> a = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> b = {0x01, 0x02, 0x03, 0x04};
    ASSERT_TRUE(tinyaes::constant_time_equal(a, b));

    b[2] = 0xFF;
    ASSERT_TRUE(!tinyaes::constant_time_equal(a, b));
}

TEST(constant_time_equal_different_sizes)
{
    std::vector<uint8_t> a = {0x01, 0x02, 0x03};
    std::vector<uint8_t> b = {0x01, 0x02, 0x03, 0x04};
    ASSERT_TRUE(!tinyaes::constant_time_equal(a, b));
}

TEST(pkcs7_pad_value_zero_rejected)
{
    // Encrypt 16 bytes of 0x00 with raw CBC (no padding).
    // The decrypted last byte will be 0x00, which is an invalid PKCS7 pad value.
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> iv(16, 0x00);
    std::vector<uint8_t> plaintext(16, 0x00);
    std::vector<uint8_t> ct;

    auto result = tinyaes::cbc_encrypt(key, iv, plaintext, ct);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.size() == 16);

    // Now try to decrypt with PKCS7 unpadding — should reject pad value 0
    std::vector<uint8_t> recovered;
    result = tinyaes::cbc_decrypt_pkcs7(key, iv, ct, recovered);
    ASSERT_TRUE(result == tinyaes::Result::InvalidPadding);
}
