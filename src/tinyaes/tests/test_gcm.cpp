// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "test_harness.h"
#include "tinyaes/gcm.h"
#include "vectors/aes_gcm_vectors.inl"

#define VEC(arr) std::vector<uint8_t>(arr, arr + sizeof(arr))
#define EMPTY_VEC std::vector<uint8_t>()

TEST(gcm_tc1_aes128_no_plaintext_no_aad)
{
    std::vector<uint8_t> ct, tag;
    auto result = tinyaes::gcm_encrypt(VEC(gcm_tc1_key), VEC(gcm_tc1_iv), EMPTY_VEC, EMPTY_VEC, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.empty());
    ASSERT_EQ(tag, VEC(gcm_tc1_tag));
}

TEST(gcm_tc2_aes128_16byte_plaintext)
{
    std::vector<uint8_t> ct, tag;
    auto result = tinyaes::gcm_encrypt(VEC(gcm_tc2_key), VEC(gcm_tc2_iv), EMPTY_VEC, VEC(gcm_tc2_plain), ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(gcm_tc2_cipher));
    ASSERT_EQ(tag, VEC(gcm_tc2_tag));
}

TEST(gcm_tc3_aes128_64byte_plaintext)
{
    std::vector<uint8_t> ct, tag;
    auto result = tinyaes::gcm_encrypt(VEC(gcm_tc3_key), VEC(gcm_tc3_iv), EMPTY_VEC, VEC(gcm_tc3_plain), ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(gcm_tc3_cipher));
    ASSERT_EQ(tag, VEC(gcm_tc3_tag));
}

TEST(gcm_tc4_aes128_with_aad)
{
    std::vector<uint8_t> ct, tag;
    auto result =
        tinyaes::gcm_encrypt(VEC(gcm_tc4_key), VEC(gcm_tc4_iv), VEC(gcm_tc4_aad), VEC(gcm_tc4_plain), ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(gcm_tc4_cipher));
    ASSERT_EQ(tag, VEC(gcm_tc4_tag));
}

TEST(gcm_tc13_aes256_no_plaintext_no_aad)
{
    std::vector<uint8_t> ct, tag;
    auto result = tinyaes::gcm_encrypt(VEC(gcm_tc13_key), VEC(gcm_tc13_iv), EMPTY_VEC, EMPTY_VEC, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.empty());
    ASSERT_EQ(tag, VEC(gcm_tc13_tag));
}

TEST(gcm_tc14_aes256_16byte_plaintext)
{
    std::vector<uint8_t> ct, tag;
    auto result = tinyaes::gcm_encrypt(VEC(gcm_tc14_key), VEC(gcm_tc14_iv), EMPTY_VEC, VEC(gcm_tc14_plain), ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(gcm_tc14_cipher));
    ASSERT_EQ(tag, VEC(gcm_tc14_tag));
}

TEST(gcm_tc2_decrypt_verify)
{
    std::vector<uint8_t> pt;
    auto result =
        tinyaes::gcm_decrypt(VEC(gcm_tc2_key), VEC(gcm_tc2_iv), EMPTY_VEC, VEC(gcm_tc2_cipher), VEC(gcm_tc2_tag), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(gcm_tc2_plain));
}

TEST(gcm_tc3_decrypt_verify)
{
    std::vector<uint8_t> pt;
    auto result =
        tinyaes::gcm_decrypt(VEC(gcm_tc3_key), VEC(gcm_tc3_iv), EMPTY_VEC, VEC(gcm_tc3_cipher), VEC(gcm_tc3_tag), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(gcm_tc3_plain));
}

TEST(gcm_tc4_decrypt_verify)
{
    std::vector<uint8_t> pt;
    auto result = tinyaes::gcm_decrypt(
        VEC(gcm_tc4_key), VEC(gcm_tc4_iv), VEC(gcm_tc4_aad), VEC(gcm_tc4_cipher), VEC(gcm_tc4_tag), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(gcm_tc4_plain));
}

TEST(gcm_roundtrip)
{
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> iv(12, 0x01);
    std::vector<uint8_t> aad = {0xAA, 0xBB, 0xCC};
    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f}; // "Hello"
    std::vector<uint8_t> ct, tag, pt;

    auto result = tinyaes::gcm_encrypt(key, iv, aad, plaintext, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);

    result = tinyaes::gcm_decrypt(key, iv, aad, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(gcm_invalid_nonce_size)
{
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> iv; // empty
    std::vector<uint8_t> plaintext = {0x01};
    std::vector<uint8_t> ct, tag;

    auto result = tinyaes::gcm_encrypt(key, iv, EMPTY_VEC, plaintext, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::InvalidIVSize);
}

TEST(gcm_combined_ct_tag_roundtrip)
{
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> nonce(12, 0x01);
    std::vector<uint8_t> aad = {0xAA, 0xBB};
    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    std::vector<uint8_t> ct_tag, pt;

    auto result = tinyaes::gcm_encrypt(key, nonce, plaintext, aad, ct_tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct_tag.size() == 21); // 5 ct + 16 tag

    result = tinyaes::gcm_decrypt(key, nonce, ct_tag, aad, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(gcm_auto_nonce_roundtrip)
{
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> aad = {0xCC};
    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    std::vector<uint8_t> nonce_ct_tag, pt;

    auto result = tinyaes::gcm_encrypt(key, plaintext, aad, nonce_ct_tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(nonce_ct_tag.size() == 33); // 12 nonce + 5 ct + 16 tag

    result = tinyaes::gcm_decrypt(key, nonce_ct_tag, aad, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, plaintext);
}

TEST(gcm_invalid_key_size)
{
    std::vector<uint8_t> key(15, 0x42); // invalid: not 16/24/32
    std::vector<uint8_t> iv(12, 0x01);
    std::vector<uint8_t> plaintext = {0x01};
    std::vector<uint8_t> ct, tag;

    auto result = tinyaes::gcm_encrypt(key, iv, EMPTY_VEC, plaintext, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::InvalidKeySize);
}

TEST(gcm_aad_only_no_plaintext)
{
    std::vector<uint8_t> key(16, 0x42);
    std::vector<uint8_t> iv(12, 0x01);
    std::vector<uint8_t> aad = {0xAA, 0xBB, 0xCC, 0xDD};
    std::vector<uint8_t> ct, tag, pt;

    auto result = tinyaes::gcm_encrypt(key, iv, aad, EMPTY_VEC, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.empty());
    ASSERT_TRUE(tag.size() == 16);

    // Decrypt with correct AAD
    result = tinyaes::gcm_decrypt(key, iv, aad, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(pt.empty());

    // Tampered AAD must fail
    std::vector<uint8_t> bad_aad = aad;
    bad_aad[0] ^= 0x01;
    result = tinyaes::gcm_decrypt(key, iv, bad_aad, ct, tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::AuthenticationFailed);
}

TEST(gcm_tc7_aes192_no_plaintext_no_aad)
{
    std::vector<uint8_t> ct, tag;
    auto result = tinyaes::gcm_encrypt(VEC(gcm_tc7_key), VEC(gcm_tc7_iv), EMPTY_VEC, EMPTY_VEC, ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_TRUE(ct.empty());
    ASSERT_EQ(tag, VEC(gcm_tc7_tag));
}

TEST(gcm_tc8_aes192_16byte_plaintext)
{
    std::vector<uint8_t> ct, tag;
    auto result = tinyaes::gcm_encrypt(VEC(gcm_tc8_key), VEC(gcm_tc8_iv), EMPTY_VEC, VEC(gcm_tc8_plain), ct, tag);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(ct, VEC(gcm_tc8_cipher));
    ASSERT_EQ(tag, VEC(gcm_tc8_tag));
}

TEST(gcm_tc8_decrypt_verify)
{
    std::vector<uint8_t> pt;
    auto result =
        tinyaes::gcm_decrypt(VEC(gcm_tc8_key), VEC(gcm_tc8_iv), EMPTY_VEC, VEC(gcm_tc8_cipher), VEC(gcm_tc8_tag), pt);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(pt, VEC(gcm_tc8_plain));
}

TEST(gcm_tc8_tampered_tag)
{
    std::vector<uint8_t> bad_tag = VEC(gcm_tc8_tag);
    bad_tag[0] ^= 0x01;
    std::vector<uint8_t> pt;
    auto result = tinyaes::gcm_decrypt(VEC(gcm_tc8_key), VEC(gcm_tc8_iv), EMPTY_VEC, VEC(gcm_tc8_cipher), bad_tag, pt);
    ASSERT_TRUE(result == tinyaes::Result::AuthenticationFailed);
}

#undef VEC
#undef EMPTY_VEC
