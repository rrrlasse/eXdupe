// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "internal/aes_impl.h"
#include "test_harness.h"
#include "vectors/aes_keyschedule_vectors.inl"

// Test portable key expansion directly (always big-endian uint32_t format)
TEST(keyschedule_portable_aes128)
{
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS] = {0};
    tinyaes::internal::aes_key_expand_portable(ks_128_key, 16, rk);

    // Verify all 44 words in big-endian format
    std::vector<uint8_t> got, expected;
    for (int i = 0; i < 44; ++i)
    {
        uint8_t buf[4];
        buf[0] = static_cast<uint8_t>(rk[i] >> 24);
        buf[1] = static_cast<uint8_t>(rk[i] >> 16);
        buf[2] = static_cast<uint8_t>(rk[i] >> 8);
        buf[3] = static_cast<uint8_t>(rk[i]);
        got.insert(got.end(), buf, buf + 4);

        buf[0] = static_cast<uint8_t>(ks_128_expected[i] >> 24);
        buf[1] = static_cast<uint8_t>(ks_128_expected[i] >> 16);
        buf[2] = static_cast<uint8_t>(ks_128_expected[i] >> 8);
        buf[3] = static_cast<uint8_t>(ks_128_expected[i]);
        expected.insert(expected.end(), buf, buf + 4);
    }
    ASSERT_EQ(got, expected);
}

TEST(keyschedule_portable_aes192_first_words)
{
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS] = {0};
    tinyaes::internal::aes_key_expand_portable(ks_192_key, 24, rk);

    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(rk[i] == ks_192_expected_first[i]);
    }
}

TEST(keyschedule_portable_aes256_first_words)
{
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS] = {0};
    tinyaes::internal::aes_key_expand_portable(ks_256_key, 32, rk);

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_TRUE(rk[i] == ks_256_expected_first[i]);
    }
}

// Functional test: dispatched key expand + encrypt + decrypt roundtrip
TEST(keyschedule_dispatch_roundtrip_128)
{
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS] = {0};
    auto key_expand = tinyaes::internal::get_key_expand();
    auto encrypt_block = tinyaes::internal::get_encrypt_block();
    auto decrypt_block = tinyaes::internal::get_decrypt_block();

    key_expand(ks_128_key, 16, rk);

    uint8_t plain[16] = {
        0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d, 0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34};
    uint8_t cipher[16], recovered[16];
    encrypt_block(rk, 10, plain, cipher);
    decrypt_block(rk, 10, cipher, recovered);

    std::vector<uint8_t> p(plain, plain + 16);
    std::vector<uint8_t> r(recovered, recovered + 16);
    ASSERT_EQ(p, r);
}

TEST(keyschedule_dispatch_roundtrip_192)
{
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS] = {0};
    auto key_expand = tinyaes::internal::get_key_expand();
    auto encrypt_block = tinyaes::internal::get_encrypt_block();
    auto decrypt_block = tinyaes::internal::get_decrypt_block();

    key_expand(ks_192_key, 24, rk);

    uint8_t plain[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
    uint8_t cipher[16], recovered[16];
    encrypt_block(rk, 12, plain, cipher);
    decrypt_block(rk, 12, cipher, recovered);

    std::vector<uint8_t> p(plain, plain + 16);
    std::vector<uint8_t> r(recovered, recovered + 16);
    ASSERT_EQ(p, r);
}

TEST(keyschedule_dispatch_roundtrip_256)
{
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS] = {0};
    auto key_expand = tinyaes::internal::get_key_expand();
    auto encrypt_block = tinyaes::internal::get_encrypt_block();
    auto decrypt_block = tinyaes::internal::get_decrypt_block();

    key_expand(ks_256_key, 32, rk);

    uint8_t plain[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
    uint8_t cipher[16], recovered[16];
    encrypt_block(rk, 14, plain, cipher);
    decrypt_block(rk, 14, cipher, recovered);

    std::vector<uint8_t> p(plain, plain + 16);
    std::vector<uint8_t> r(recovered, recovered + 16);
    ASSERT_EQ(p, r);
}
