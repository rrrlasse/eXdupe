// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "test_harness.h"
#include "tinyaes/cbc.h"
#include "tinyaes/common.h"
#include "tinyaes/ctr.h"
#include "tinyaes/ecb.h"
#include "tinyaes/gcm.h"
#include "vectors/aes_ecb_vectors.inl"

#include <cstring>

TEST(capi_ecb_encrypt_128)
{
    uint8_t ct[16];
    int rc =
        tinyaes_ecb_encrypt(ecb_128_key, sizeof(ecb_128_key), ecb_128_plain, sizeof(ecb_128_plain), ct, sizeof(ct));
    ASSERT_TRUE(rc == TINYAES_OK);
    ASSERT_TRUE(std::memcmp(ct, ecb_128_cipher, 16) == 0);
}

TEST(capi_ecb_decrypt_128)
{
    uint8_t pt[16];
    int rc =
        tinyaes_ecb_decrypt(ecb_128_key, sizeof(ecb_128_key), ecb_128_cipher, sizeof(ecb_128_cipher), pt, sizeof(pt));
    ASSERT_TRUE(rc == TINYAES_OK);
    ASSERT_TRUE(std::memcmp(pt, ecb_128_plain, 16) == 0);
}

TEST(capi_ecb_invalid_key)
{
    uint8_t key[15] = {0};
    uint8_t pt[16] = {0};
    uint8_t ct[16];
    int rc = tinyaes_ecb_encrypt(key, sizeof(key), pt, sizeof(pt), ct, sizeof(ct));
    ASSERT_TRUE(rc == TINYAES_INVALID_KEY_SIZE);
}

TEST(capi_ecb_null_input)
{
    uint8_t key[16] = {0};
    uint8_t ct[16];
    int rc = tinyaes_ecb_encrypt(key, sizeof(key), nullptr, 16, ct, sizeof(ct));
    ASSERT_TRUE(rc == TINYAES_INVALID_INPUT_SIZE);
}

TEST(capi_cbc_pkcs7_roundtrip)
{
    uint8_t key[16] = {0x42};
    uint8_t iv[16] = {0};
    uint8_t pt[5] = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    uint8_t ct[32]; // 5 bytes padded to 16
    size_t ct_len = sizeof(ct);

    int rc = tinyaes_cbc_encrypt_pkcs7(key, sizeof(key), iv, pt, sizeof(pt), ct, &ct_len);
    ASSERT_TRUE(rc == TINYAES_OK);
    ASSERT_TRUE(ct_len == 16);

    uint8_t recovered[32];
    size_t recovered_len = sizeof(recovered);
    rc = tinyaes_cbc_decrypt_pkcs7(key, sizeof(key), iv, ct, ct_len, recovered, &recovered_len);
    ASSERT_TRUE(rc == TINYAES_OK);
    ASSERT_TRUE(recovered_len == 5);
    ASSERT_TRUE(std::memcmp(recovered, pt, 5) == 0);
}

TEST(capi_cbc_pkcs7_empty_roundtrip)
{
    // Regression: UBSan flagged a nullptr-to-memcpy on the empty-plaintext
    // PKCS#7 encrypt path (empty vector's data() returns nullptr, and libc
    // memcpy's src is declared nonnull even when n == 0). Covers both the
    // C++ encrypt path (cbc.cpp pkcs7 encrypt) and the C API decrypt path
    // where the recovered plaintext buffer is empty.
    uint8_t key[16] = {0x42};
    uint8_t iv[16] = {0};
    uint8_t ct[16];
    size_t ct_len = sizeof(ct);

    // Encrypt zero-length plaintext — a dummy non-null src pointer is
    // required to pass the C API's null-input guard, but plaintext_len = 0
    // means no bytes should actually be read from it.
    uint8_t dummy = 0;
    int rc = tinyaes_cbc_encrypt_pkcs7(key, sizeof(key), iv, &dummy, 0, ct, &ct_len);
    ASSERT_TRUE(rc == TINYAES_OK);
    ASSERT_TRUE(ct_len == 16); // full block of 0x10 padding

    // Decrypt back to empty plaintext. recovered_len must be reported as 0
    // and no bytes written to the output buffer.
    uint8_t recovered[16];
    size_t recovered_len = sizeof(recovered);
    rc = tinyaes_cbc_decrypt_pkcs7(key, sizeof(key), iv, ct, ct_len, recovered, &recovered_len);
    ASSERT_TRUE(rc == TINYAES_OK);
    ASSERT_TRUE(recovered_len == 0);
}

TEST(capi_cbc_null_iv)
{
    uint8_t key[16] = {0};
    uint8_t pt[16] = {0};
    uint8_t ct[16];
    int rc = tinyaes_cbc_encrypt(key, sizeof(key), nullptr, pt, sizeof(pt), ct, sizeof(ct));
    ASSERT_TRUE(rc == TINYAES_INVALID_INPUT_SIZE);
}

TEST(capi_ctr_crypt_roundtrip)
{
    uint8_t key[16] = {0x42};
    uint8_t iv[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x00, 0x00, 0x00, 0x01};
    uint8_t pt[7] = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x21, 0x21};
    uint8_t ct[7];
    uint8_t recovered[7];

    int rc = tinyaes_ctr_crypt(key, sizeof(key), iv, pt, sizeof(pt), ct, sizeof(ct));
    ASSERT_TRUE(rc == TINYAES_OK);

    rc = tinyaes_ctr_crypt(key, sizeof(key), iv, ct, sizeof(ct), recovered, sizeof(recovered));
    ASSERT_TRUE(rc == TINYAES_OK);
    ASSERT_TRUE(std::memcmp(recovered, pt, sizeof(pt)) == 0);
}

TEST(capi_ctr_nonce_roundtrip)
{
    uint8_t key[16] = {0x42};
    uint8_t nonce[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
    uint8_t pt[5] = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    uint8_t ct[5];
    uint8_t recovered[5];

    int rc = tinyaes_ctr_encrypt(key, sizeof(key), nonce, pt, sizeof(pt), ct, sizeof(ct));
    ASSERT_TRUE(rc == TINYAES_OK);

    rc = tinyaes_ctr_decrypt(key, sizeof(key), nonce, ct, sizeof(ct), recovered, sizeof(recovered));
    ASSERT_TRUE(rc == TINYAES_OK);
    ASSERT_TRUE(std::memcmp(recovered, pt, sizeof(pt)) == 0);
}

TEST(capi_gcm_encrypt_decrypt)
{
    uint8_t key[16] = {0xAA};
    uint8_t iv[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
    uint8_t aad[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t pt[32];
    std::memset(pt, 0x55, sizeof(pt));
    uint8_t ct[32];
    uint8_t tag[16];

    int rc =
        tinyaes_gcm_encrypt(key, sizeof(key), iv, sizeof(iv), aad, sizeof(aad), pt, sizeof(pt), ct, sizeof(ct), tag);
    ASSERT_TRUE(rc == TINYAES_OK);

    uint8_t recovered[32];
    rc = tinyaes_gcm_decrypt(
        key, sizeof(key), iv, sizeof(iv), aad, sizeof(aad), ct, sizeof(ct), recovered, sizeof(recovered), tag);
    ASSERT_TRUE(rc == TINYAES_OK);
    ASSERT_TRUE(std::memcmp(recovered, pt, sizeof(pt)) == 0);
}

TEST(capi_gcm_auth_failure)
{
    uint8_t key[16] = {0xAA};
    uint8_t iv[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
    uint8_t pt[16] = {0x42};
    uint8_t ct[16];
    uint8_t tag[16];

    int rc = tinyaes_gcm_encrypt(key, sizeof(key), iv, sizeof(iv), nullptr, 0, pt, sizeof(pt), ct, sizeof(ct), tag);
    ASSERT_TRUE(rc == TINYAES_OK);

    // Tamper with tag
    tag[0] ^= 0x01;
    uint8_t recovered[16];
    rc = tinyaes_gcm_decrypt(
        key, sizeof(key), iv, sizeof(iv), nullptr, 0, ct, sizeof(ct), recovered, sizeof(recovered), tag);
    ASSERT_TRUE(rc == TINYAES_AUTH_FAILED);
}

TEST(capi_gcm_null_key)
{
    uint8_t iv[12] = {0};
    uint8_t pt[16] = {0};
    uint8_t ct[16];
    uint8_t tag[16];
    int rc = tinyaes_gcm_encrypt(nullptr, 16, iv, sizeof(iv), nullptr, 0, pt, sizeof(pt), ct, sizeof(ct), tag);
    ASSERT_TRUE(rc == TINYAES_INVALID_INPUT_SIZE);
}

TEST(capi_generate_iv)
{
    uint8_t iv[16] = {0};
    int rc = tinyaes_generate_iv(iv);
    ASSERT_TRUE(rc == TINYAES_OK);

    // Verify not all zeros (extremely unlikely for 128 random bits)
    bool all_zero = true;
    for (int i = 0; i < 16; ++i)
    {
        if (iv[i] != 0)
        {
            all_zero = false;
            break;
        }
    }
    ASSERT_TRUE(!all_zero);
}

TEST(capi_generate_nonce)
{
    uint8_t nonce[12] = {0};
    int rc = tinyaes_generate_nonce(nonce);
    ASSERT_TRUE(rc == TINYAES_OK);

    bool all_zero = true;
    for (int i = 0; i < 12; ++i)
    {
        if (nonce[i] != 0)
        {
            all_zero = false;
            break;
        }
    }
    ASSERT_TRUE(!all_zero);
}

TEST(capi_constant_time_equal)
{
    uint8_t a[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    uint8_t b[16];
    std::memcpy(b, a, 16);

    ASSERT_TRUE(tinyaes_constant_time_equal(a, b, 16) == 1);

    b[15] ^= 0x01;
    ASSERT_TRUE(tinyaes_constant_time_equal(a, b, 16) == 0);
}

#undef VEC
