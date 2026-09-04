// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "internal/aes_impl.h"
#include "internal/ghash.h"
#include "test_harness.h"

#include <cstring>

static const uint8_t test_block[16] =
    {0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d, 0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34};

// --- Encrypt block differential ---

TEST(diff_encrypt_block_aes128)
{
    uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    uint32_t rk_p[tinyaes::internal::AES_MAX_RK_WORDS];
    uint32_t rk_d[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, 16, rk_p);
    tinyaes::internal::get_key_expand()(key, 16, rk_d);

    uint8_t out_p[16], out_d[16];
    tinyaes::internal::aes_encrypt_block_portable(rk_p, 10, test_block, out_p);
    tinyaes::internal::get_encrypt_block()(rk_d, 10, test_block, out_d);
    ASSERT_TRUE(std::memcmp(out_p, out_d, 16) == 0);
}

TEST(diff_encrypt_block_aes192)
{
    uint8_t key[24];
    std::memset(key, 0xAB, sizeof(key));
    uint32_t rk_p[tinyaes::internal::AES_MAX_RK_WORDS];
    uint32_t rk_d[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, 24, rk_p);
    tinyaes::internal::get_key_expand()(key, 24, rk_d);

    uint8_t out_p[16], out_d[16];
    tinyaes::internal::aes_encrypt_block_portable(rk_p, 12, test_block, out_p);
    tinyaes::internal::get_encrypt_block()(rk_d, 12, test_block, out_d);
    ASSERT_TRUE(std::memcmp(out_p, out_d, 16) == 0);
}

TEST(diff_encrypt_block_aes256)
{
    uint8_t key[32];
    std::memset(key, 0xCD, sizeof(key));
    uint32_t rk_p[tinyaes::internal::AES_MAX_RK_WORDS];
    uint32_t rk_d[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, 32, rk_p);
    tinyaes::internal::get_key_expand()(key, 32, rk_d);

    uint8_t out_p[16], out_d[16];
    tinyaes::internal::aes_encrypt_block_portable(rk_p, 14, test_block, out_p);
    tinyaes::internal::get_encrypt_block()(rk_d, 14, test_block, out_d);
    ASSERT_TRUE(std::memcmp(out_p, out_d, 16) == 0);
}

// --- Decrypt block differential ---
// Each backend uses its own key expansion since internal formats may differ.

TEST(diff_decrypt_block_aes128)
{
    uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    uint32_t rk_p[tinyaes::internal::AES_MAX_RK_WORDS];
    uint32_t rk_d[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, 16, rk_p);
    tinyaes::internal::get_key_expand()(key, 16, rk_d);

    // Encrypt with portable to get ciphertext
    uint8_t ct[16];
    tinyaes::internal::aes_encrypt_block_portable(rk_p, 10, test_block, ct);

    // Decrypt with each backend using its own round keys
    uint8_t pt_p[16], pt_d[16];
    tinyaes::internal::aes_decrypt_block_portable(rk_p, 10, ct, pt_p);
    tinyaes::internal::get_decrypt_block()(rk_d, 10, ct, pt_d);
    ASSERT_TRUE(std::memcmp(pt_p, pt_d, 16) == 0);
    ASSERT_TRUE(std::memcmp(pt_p, test_block, 16) == 0);
}

// --- Key expansion differential ---
// AES-NI key expansion may use a different internal format, so we compare
// the functional result (encrypt output) rather than raw round key words.

TEST(diff_key_expand_aes128)
{
    uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    uint32_t rk_p[tinyaes::internal::AES_MAX_RK_WORDS];
    uint32_t rk_d[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, 16, rk_p);
    tinyaes::internal::get_key_expand()(key, 16, rk_d);

    // Verify both produce the same encrypted output
    uint8_t out_p[16], out_d[16];
    tinyaes::internal::aes_encrypt_block_portable(rk_p, 10, test_block, out_p);
    tinyaes::internal::get_encrypt_block()(rk_d, 10, test_block, out_d);
    ASSERT_TRUE(std::memcmp(out_p, out_d, 16) == 0);
}

TEST(diff_key_expand_aes256)
{
    uint8_t key[32];
    std::memset(key, 0xEF, sizeof(key));
    uint32_t rk_p[tinyaes::internal::AES_MAX_RK_WORDS];
    uint32_t rk_d[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, 32, rk_p);
    tinyaes::internal::get_key_expand()(key, 32, rk_d);

    // Verify both produce the same encrypted output
    uint8_t out_p[16], out_d[16];
    tinyaes::internal::aes_encrypt_block_portable(rk_p, 14, test_block, out_p);
    tinyaes::internal::get_encrypt_block()(rk_d, 14, test_block, out_d);
    ASSERT_TRUE(std::memcmp(out_p, out_d, 16) == 0);
}

// --- CTR pipeline differential ---
// Each pipeline uses its own key expansion to match internal format
// expectations.

TEST(diff_ctr_pipeline_4_blocks)
{
    uint8_t key[16];
    std::memset(key, 0x42, sizeof(key));
    uint32_t rk_p[tinyaes::internal::AES_MAX_RK_WORDS];
    uint32_t rk_d[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, 16, rk_p);
    tinyaes::internal::get_key_expand()(key, 16, rk_d);

    uint8_t input[64];
    std::memset(input, 0x55, sizeof(input));

    uint8_t out_p[64], out_d[64];
    uint8_t ctr_p[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x00, 0x00, 0x00, 0x01};
    uint8_t ctr_d[16];
    std::memcpy(ctr_d, ctr_p, 16);

    tinyaes::internal::aes_ctr_pipeline_portable(rk_p, 10, input, out_p, 4, ctr_p);
    tinyaes::internal::get_ctr_pipeline()(rk_d, 10, input, out_d, 4, ctr_d);

    ASSERT_TRUE(std::memcmp(out_p, out_d, 64) == 0);
    ASSERT_TRUE(std::memcmp(ctr_p, ctr_d, 16) == 0);
}

TEST(diff_ctr_pipeline_9_blocks)
{
    uint8_t key[16];
    std::memset(key, 0x77, sizeof(key));
    uint32_t rk_p[tinyaes::internal::AES_MAX_RK_WORDS];
    uint32_t rk_d[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key, 16, rk_p);
    tinyaes::internal::get_key_expand()(key, 16, rk_d);

    uint8_t input[144]; // 9 blocks
    std::memset(input, 0xAA, sizeof(input));

    uint8_t out_p[144], out_d[144];
    uint8_t ctr_p[16] = {
        0xCA, 0xFE, 0xBA, 0xBE, 0xFA, 0xCE, 0xDB, 0xAD, 0xDE, 0xCA, 0xF8, 0x88, 0x00, 0x00, 0x00, 0x01};
    uint8_t ctr_d[16];
    std::memcpy(ctr_d, ctr_p, 16);

    tinyaes::internal::aes_ctr_pipeline_portable(rk_p, 10, input, out_p, 9, ctr_p);
    tinyaes::internal::get_ctr_pipeline()(rk_d, 10, input, out_d, 9, ctr_d);

    ASSERT_TRUE(std::memcmp(out_p, out_d, 144) == 0);
    ASSERT_TRUE(std::memcmp(ctr_p, ctr_d, 16) == 0);
}

// --- GHASH differential ---

TEST(diff_ghash_single_block)
{
    // Use a fixed H (hash subkey)
    uint8_t H[16] = {0x66, 0xe9, 0x4b, 0xd4, 0xef, 0x8a, 0x2c, 0x3b, 0x88, 0x4c, 0xfa, 0x59, 0xca, 0x34, 0x2b, 0x2e};
    uint8_t data[16];
    std::memset(data, 0x55, sizeof(data));

    uint8_t Y_p[16] = {0};
    uint8_t Y_d[16] = {0};
    tinyaes::internal::ghash_portable(H, data, sizeof(data), Y_p);
    tinyaes::internal::get_ghash()(H, data, sizeof(data), Y_d);
    ASSERT_TRUE(std::memcmp(Y_p, Y_d, 16) == 0);
}

TEST(diff_ghash_multi_block)
{
    uint8_t H[16] = {0x66, 0xe9, 0x4b, 0xd4, 0xef, 0x8a, 0x2c, 0x3b, 0x88, 0x4c, 0xfa, 0x59, 0xca, 0x34, 0x2b, 0x2e};
    uint8_t data[48]; // 3 blocks
    std::memset(data, 0xAB, sizeof(data));

    uint8_t Y_p[16] = {0};
    uint8_t Y_d[16] = {0};
    tinyaes::internal::ghash_portable(H, data, sizeof(data), Y_p);
    tinyaes::internal::get_ghash()(H, data, sizeof(data), Y_d);
    ASSERT_TRUE(std::memcmp(Y_p, Y_d, 16) == 0);
}

TEST(diff_ghash_partial_block)
{
    uint8_t H[16] = {0x66, 0xe9, 0x4b, 0xd4, 0xef, 0x8a, 0x2c, 0x3b, 0x88, 0x4c, 0xfa, 0x59, 0xca, 0x34, 0x2b, 0x2e};
    uint8_t data[20]; // 1 full block + 4 bytes
    std::memset(data, 0xCD, sizeof(data));

    uint8_t Y_p[16] = {0};
    uint8_t Y_d[16] = {0};
    tinyaes::internal::ghash_portable(H, data, sizeof(data), Y_p);
    tinyaes::internal::get_ghash()(H, data, sizeof(data), Y_d);
    ASSERT_TRUE(std::memcmp(Y_p, Y_d, 16) == 0);
}
