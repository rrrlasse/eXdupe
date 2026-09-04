// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "test_harness.h"
#include "tinyaes/gcm.h"
#include "vectors/aes_gcm_vectors_extended.inl"

#define VEC(arr) std::vector<uint8_t>(arr, arr + sizeof(arr))

TEST(gcm_nist_tc5_8byte_iv)
{
    std::vector<uint8_t> ct, tag;
    auto result =
        tinyaes::gcm_encrypt(VEC(gcm_tc5_key), VEC(gcm_tc5_iv), VEC(gcm_tc5_aad), VEC(gcm_tc5_plain), ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(gcm_tc5_cipher));
    ASSERT_EQ(tag, VEC(gcm_tc5_tag));
}

TEST(gcm_nist_tc5_8byte_iv_decrypt)
{
    std::vector<uint8_t> pt;
    auto result = tinyaes::gcm_decrypt(
        VEC(gcm_tc5_key), VEC(gcm_tc5_iv), VEC(gcm_tc5_aad), VEC(gcm_tc5_cipher), VEC(gcm_tc5_tag), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(gcm_tc5_plain));
}

TEST(gcm_nist_tc6_60byte_iv)
{
    std::vector<uint8_t> ct, tag;
    auto result =
        tinyaes::gcm_encrypt(VEC(gcm_tc6_key), VEC(gcm_tc6_iv), VEC(gcm_tc6_aad), VEC(gcm_tc6_plain), ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(gcm_tc6_cipher));
    ASSERT_EQ(tag, VEC(gcm_tc6_tag));
}

TEST(gcm_nist_tc6_60byte_iv_decrypt)
{
    std::vector<uint8_t> pt;
    auto result = tinyaes::gcm_decrypt(
        VEC(gcm_tc6_key), VEC(gcm_tc6_iv), VEC(gcm_tc6_aad), VEC(gcm_tc6_cipher), VEC(gcm_tc6_tag), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(gcm_tc6_plain));
}

TEST(gcm_non_12byte_iv_1byte)
{
    std::vector<uint8_t> key(16, 0xAA);
    std::vector<uint8_t> iv = {0x42};
    std::vector<uint8_t> aad = {0xDE, 0xAD};
    std::vector<uint8_t> plaintext(32, 0x55);
    std::vector<uint8_t> ct, tag, pt;

    auto result = tinyaes::gcm_encrypt(key, iv, aad, plaintext, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);

    result = tinyaes::gcm_decrypt(key, iv, aad, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(gcm_non_12byte_iv_16byte)
{
    std::vector<uint8_t> key(16, 0xBB);
    std::vector<uint8_t> iv(16, 0x01);
    std::vector<uint8_t> aad = {0xFE, 0xED};
    std::vector<uint8_t> plaintext(48, 0x33);
    std::vector<uint8_t> ct, tag, pt;

    auto result = tinyaes::gcm_encrypt(key, iv, aad, plaintext, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);

    result = tinyaes::gcm_decrypt(key, iv, aad, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(gcm_non_12byte_iv_32byte)
{
    std::vector<uint8_t> key(32, 0xCC); // AES-256
    std::vector<uint8_t> iv(32, 0x02);
    std::vector<uint8_t> aad;
    std::vector<uint8_t> plaintext(64, 0x77);
    std::vector<uint8_t> ct, tag, pt;

    auto result = tinyaes::gcm_encrypt(key, iv, aad, plaintext, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);

    result = tinyaes::gcm_decrypt(key, iv, aad, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(gcm_non_12byte_iv_empty_plaintext)
{
    std::vector<uint8_t> key(16, 0xDD);
    std::vector<uint8_t> iv(20, 0x03);
    std::vector<uint8_t> aad = {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89};
    std::vector<uint8_t> plaintext; // empty
    std::vector<uint8_t> ct, tag, pt;

    auto result = tinyaes::gcm_encrypt(key, iv, aad, plaintext, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.empty());
    ASSERT_TRUE(tag.size() == 16);

    result = tinyaes::gcm_decrypt(key, iv, aad, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(pt.empty());
}

TEST(gcm_aes192_roundtrip_with_aad)
{
    std::vector<uint8_t> key(24, 0xEE); // AES-192
    std::vector<uint8_t> iv(12, 0x04);
    std::vector<uint8_t> aad = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    std::vector<uint8_t> plaintext(48, 0x99);
    std::vector<uint8_t> ct, tag, pt;

    auto result = tinyaes::gcm_encrypt(key, iv, aad, plaintext, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);

    result = tinyaes::gcm_decrypt(key, iv, aad, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(gcm_combined_format_auth_failure)
{
    std::vector<uint8_t> key(16, 0xFF);
    std::vector<uint8_t> nonce(12, 0x05);
    std::vector<uint8_t> aad = {0xAA, 0xBB};
    std::vector<uint8_t> plaintext(32, 0x42);
    std::vector<uint8_t> ct_tag;

    auto result = tinyaes::gcm_encrypt(key, nonce, plaintext, aad, ct_tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct_tag.size() == 32 + 16);

    // Tamper with ciphertext portion (first byte)
    ct_tag[0] ^= 0x01;

    std::vector<uint8_t> pt;
    result = tinyaes::gcm_decrypt(key, nonce, ct_tag, aad, pt);
    ASSERT_TRUE(result == tinyaes::Result::AuthenticationFailed);
}

TEST(gcm_auto_nonce_auth_failure)
{
    std::vector<uint8_t> key(16, 0x11);
    std::vector<uint8_t> aad = {0xCC, 0xDD};
    std::vector<uint8_t> plaintext(16, 0x22);
    std::vector<uint8_t> nonce_ct_tag;

    auto result = tinyaes::gcm_encrypt(key, plaintext, aad, nonce_ct_tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(nonce_ct_tag.size() == 12 + 16 + 16);

    // Tamper with ciphertext portion (at offset 12)
    nonce_ct_tag[12] ^= 0x01;

    std::vector<uint8_t> pt;
    result = tinyaes::gcm_decrypt(key, nonce_ct_tag, aad, pt);
    ASSERT_TRUE(result == tinyaes::Result::AuthenticationFailed);
}

#undef VEC
