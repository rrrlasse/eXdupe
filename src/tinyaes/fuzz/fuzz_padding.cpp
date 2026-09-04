// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "tinyaes/cbc.h"

#include <cassert>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;

    // First byte selects key size: 16, 24, or 32
    static const size_t key_sizes[] = {16, 24, 32};
    size_t key_len = key_sizes[data[0] % 3];
    uint8_t tamper_byte = data[1];
    data += 2;
    size -= 2;

    // Need key + 16-byte IV + at least 1 byte of plaintext
    if (size < key_len + 17)
        return 0;

    std::vector<uint8_t> key(data, data + key_len);
    std::vector<uint8_t> iv(data + key_len, data + key_len + 16);
    std::vector<uint8_t> plaintext(data + key_len + 16, data + size);

    std::vector<uint8_t> ct, pt;

    // Encrypt with PKCS7 padding
    auto result = tinyaes::cbc_encrypt_pkcs7(key, iv, plaintext, ct);
    if (result != tinyaes::Result::Ok)
        return 0;

    // Decrypt and verify roundtrip
    result = tinyaes::cbc_decrypt_pkcs7(key, iv, ct, pt);
    assert(result == tinyaes::Result::Ok);
    assert(pt == plaintext);

    // Tamper test: corrupt a byte in the last block of ciphertext
    if (!ct.empty())
    {
        std::vector<uint8_t> bad_ct = ct;
        // Target last block (where padding lives after decryption)
        size_t last_block_start = bad_ct.size() - 16;
        size_t corrupt_offset = last_block_start + (tamper_byte % 16);
        bad_ct[corrupt_offset] ^= 0x01;

        std::vector<uint8_t> bad_pt;
        auto bad_result = tinyaes::cbc_decrypt_pkcs7(key, iv, bad_ct, bad_pt);
        // Corruption in the last block should usually cause InvalidPadding,
        // but might also produce valid but different plaintext (rare).
        // Either way, it must not produce the original plaintext.
        if (bad_result == tinyaes::Result::Ok)
        {
            assert(bad_pt != plaintext);
        }
        else
        {
            assert(bad_result == tinyaes::Result::InvalidPadding);
        }
    }

    return 0;
}
