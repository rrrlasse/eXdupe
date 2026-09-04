// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "internal/aes_impl.h"
#include "internal/endian.h"
#include "test_harness.h"
#include "tinyaes/ctr.h"

#include <cstring>

// Helper: build a 16-byte IV from a 12-byte nonce and a 32-bit big-endian
// counter
static std::vector<uint8_t> make_iv(const uint8_t nonce[12], uint32_t counter)
{
    std::vector<uint8_t> iv(16, 0);
    std::memcpy(iv.data(), nonce, 12);
    tinyaes::internal::store_be32(iv.data() + 12, counter);
    return iv;
}

// Encrypt `blocks` blocks using the portable CTR pipeline directly
static std::vector<uint8_t>
    ctr_portable(const std::vector<uint8_t> &key, std::vector<uint8_t> &iv, const std::vector<uint8_t> &input)
{
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS];
    tinyaes::internal::aes_key_expand_portable(key.data(), key.size(), rk);

    size_t blocks = input.size() / 16;
    std::vector<uint8_t> output(input.size());
    uint8_t ctr[16];
    std::memcpy(ctr, iv.data(), 16);

    tinyaes::internal::aes_ctr_pipeline_portable(
        rk, tinyaes::internal::aes_rounds(key.size()), input.data(), output.data(), blocks, ctr);

    std::memcpy(iv.data(), ctr, 16);
    return output;
}

// Encrypt `blocks` blocks using the dispatched CTR pipeline
static std::vector<uint8_t>
    ctr_dispatched(const std::vector<uint8_t> &key, std::vector<uint8_t> &iv, const std::vector<uint8_t> &input)
{
    uint32_t rk[tinyaes::internal::AES_MAX_RK_WORDS];
    auto key_expand = tinyaes::internal::get_key_expand();
    key_expand(key.data(), key.size(), rk);

    size_t blocks = input.size() / 16;
    std::vector<uint8_t> output(input.size());
    uint8_t ctr[16];
    std::memcpy(ctr, iv.data(), 16);

    auto ctr_pipeline = tinyaes::internal::get_ctr_pipeline();
    ctr_pipeline(rk, tinyaes::internal::aes_rounds(key.size()), input.data(), output.data(), blocks, ctr);

    std::memcpy(iv.data(), ctr, 16);
    return output;
}

TEST(ctr_counter_wrap_3_blocks)
{
    const uint8_t nonce[12] = {0xCA, 0xFE, 0xBA, 0xBE, 0xFA, 0xCE, 0xDB, 0xAD, 0xDE, 0xCA, 0xF8, 0x88};
    std::vector<uint8_t> key(16, 0xAA); // AES-128

    // Start counter at 0xFFFFFFFE — after 3 blocks it should wrap: FE -> FF -> 00
    // -> final = 01
    auto iv_portable = make_iv(nonce, 0xFFFFFFFE);
    auto iv_dispatched = make_iv(nonce, 0xFFFFFFFE);

    // 3 blocks of plaintext (48 bytes)
    std::vector<uint8_t> plaintext(48, 0x55);

    auto ct_portable = ctr_portable(key, iv_portable, plaintext);
    auto ct_dispatched = ctr_dispatched(key, iv_dispatched, plaintext);

    // Ciphertext must match between portable and dispatched
    ASSERT_EQ(ct_portable, ct_dispatched);

    // Final counter state must match
    ASSERT_EQ(iv_portable, iv_dispatched);

    // Verify nonce bytes are preserved (first 12 bytes unchanged)
    for (int i = 0; i < 12; ++i)
    {
        ASSERT_TRUE(iv_portable[i] == nonce[i]);
    }

    // Verify counter wrapped to 0x00000001
    uint32_t final_ctr = tinyaes::internal::load_be32(iv_portable.data() + 12);
    ASSERT_TRUE(final_ctr == 0x00000001);
}

TEST(ctr_counter_wrap_5_blocks)
{
    // 5 blocks to exercise the 4-block pipeline loop + remainder
    const uint8_t nonce[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
    std::vector<uint8_t> key(32, 0xBB); // AES-256

    // Counter starts at 0xFFFFFFFD — after 5 blocks: FD -> FE -> FF -> 00 -> 01
    // -> final = 02
    auto iv_portable = make_iv(nonce, 0xFFFFFFFD);
    auto iv_dispatched = make_iv(nonce, 0xFFFFFFFD);

    std::vector<uint8_t> plaintext(80, 0x33); // 5 blocks

    auto ct_portable = ctr_portable(key, iv_portable, plaintext);
    auto ct_dispatched = ctr_dispatched(key, iv_dispatched, plaintext);

    ASSERT_EQ(ct_portable, ct_dispatched);
    ASSERT_EQ(iv_portable, iv_dispatched);

    // Nonce preserved
    for (int i = 0; i < 12; ++i)
    {
        ASSERT_TRUE(iv_portable[i] == nonce[i]);
    }

    // Counter should be 0x00000002
    uint32_t final_ctr = tinyaes::internal::load_be32(iv_portable.data() + 12);
    ASSERT_TRUE(final_ctr == 0x00000002);
}

TEST(ctr_counter_wrap_8_blocks)
{
    // 8 blocks exercises two iterations of the 4-block pipeline loop
    const uint8_t nonce[12] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    std::vector<uint8_t> key(16, 0xCC); // AES-128

    // Counter starts at 0xFFFFFFFC — after 8 blocks: FC..FF, 00..03 -> final = 04
    auto iv_portable = make_iv(nonce, 0xFFFFFFFC);
    auto iv_dispatched = make_iv(nonce, 0xFFFFFFFC);

    std::vector<uint8_t> plaintext(128, 0x77); // 8 blocks

    auto ct_portable = ctr_portable(key, iv_portable, plaintext);
    auto ct_dispatched = ctr_dispatched(key, iv_dispatched, plaintext);

    ASSERT_EQ(ct_portable, ct_dispatched);
    ASSERT_EQ(iv_portable, iv_dispatched);

    // Nonce preserved
    for (int i = 0; i < 12; ++i)
    {
        ASSERT_TRUE(iv_portable[i] == nonce[i]);
    }

    // Counter should be 0x00000004
    uint32_t final_ctr = tinyaes::internal::load_be32(iv_portable.data() + 12);
    ASSERT_TRUE(final_ctr == 0x00000004);
}

TEST(ctr_counter_wrap_roundtrip)
{
    // Verify encrypt/decrypt roundtrip works across counter wrap boundary
    const uint8_t nonce[12] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::vector<uint8_t> key(16, 0xDD);

    auto iv = make_iv(nonce, 0xFFFFFFFE);
    std::vector<uint8_t> plaintext(48, 0x42);

    // Encrypt
    std::vector<uint8_t> ciphertext;
    auto result = tinyaes::ctr_crypt(key, iv, plaintext, ciphertext);
    ASSERT_TRUE(result == tinyaes::Result::Ok);

    // Decrypt (CTR is symmetric)
    std::vector<uint8_t> recovered;
    result = tinyaes::ctr_crypt(key, iv, ciphertext, recovered);
    ASSERT_TRUE(result == tinyaes::Result::Ok);
    ASSERT_EQ(recovered, plaintext);
}
