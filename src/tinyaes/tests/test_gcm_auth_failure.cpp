// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "test_harness.h"
#include "tinyaes/gcm.h"

static std::vector<uint8_t> make_key()
{
    return std::vector<uint8_t>(16, 0x42);
}
static std::vector<uint8_t> make_iv()
{
    return std::vector<uint8_t>(12, 0x01);
}

// Helper: encrypt a known message
static void encrypt_helper(
    std::vector<uint8_t> &ct,
    std::vector<uint8_t> &tag,
    const std::vector<uint8_t> &aad = {},
    const std::vector<uint8_t> &pt = {0x48, 0x65, 0x6c, 0x6c, 0x6f})
{
    auto key = make_key();
    auto iv = make_iv();
    tinyaes::gcm_encrypt(key, iv, aad, pt, ct, tag);
}

TEST(gcm_auth_fail_tampered_ciphertext)
{
    std::vector<uint8_t> ct, tag;
    encrypt_helper(ct, tag);

    // Tamper with ciphertext
    ct[0] ^= 0x01;

    std::vector<uint8_t> pt;
    auto result = tinyaes::gcm_decrypt(make_key(), make_iv(), {}, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::AuthenticationFailed);
    ASSERT_TRUE(pt.empty());
}

TEST(gcm_auth_fail_tampered_tag)
{
    std::vector<uint8_t> ct, tag;
    encrypt_helper(ct, tag);

    // Tamper with tag
    tag[0] ^= 0x01;

    std::vector<uint8_t> pt;
    auto result = tinyaes::gcm_decrypt(make_key(), make_iv(), {}, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::AuthenticationFailed);
    ASSERT_TRUE(pt.empty());
}

TEST(gcm_auth_fail_tampered_aad)
{
    std::vector<uint8_t> aad = {0xAA, 0xBB, 0xCC};
    std::vector<uint8_t> ct, tag;
    encrypt_helper(ct, tag, aad);

    // Tamper with AAD
    std::vector<uint8_t> bad_aad = {0xAA, 0xBB, 0xCD};

    std::vector<uint8_t> pt;
    auto result = tinyaes::gcm_decrypt(make_key(), make_iv(), bad_aad, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::AuthenticationFailed);
    ASSERT_TRUE(pt.empty());
}

TEST(gcm_auth_fail_wrong_key)
{
    std::vector<uint8_t> ct, tag;
    encrypt_helper(ct, tag);

    // Use wrong key
    std::vector<uint8_t> wrong_key(16, 0x43);

    std::vector<uint8_t> pt;
    auto result = tinyaes::gcm_decrypt(wrong_key, make_iv(), {}, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::AuthenticationFailed);
    ASSERT_TRUE(pt.empty());
}

TEST(gcm_auth_fail_wrong_iv)
{
    std::vector<uint8_t> ct, tag;
    encrypt_helper(ct, tag);

    // Use wrong IV
    std::vector<uint8_t> wrong_iv(12, 0x02);

    std::vector<uint8_t> pt;
    auto result = tinyaes::gcm_decrypt(make_key(), wrong_iv, {}, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::AuthenticationFailed);
    ASSERT_TRUE(pt.empty());
}
