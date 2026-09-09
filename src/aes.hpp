// Minimal AES-256 block + CTR helper and SHA-256 key derivation.
// Compact implementation adapted for this project. Public domain / MIT-style usage.
#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <random>
#include <cstring>

#include "tinyaes/include/tinyaes.h"

namespace dup_crypto {

namespace pbkdf2_detail {

class sha256 {
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    uint64_t bitlen = 0;
    uint8_t buf[64]{};
    size_t buflen = 0;

    static constexpr uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

    static constexpr uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }

    static constexpr uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }

    static constexpr uint32_t bsig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }

    static constexpr uint32_t bsig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }

    static constexpr uint32_t ssig0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }

    static constexpr uint32_t ssig1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

    static constexpr uint32_t K[64] = {0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
                                       0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
                                       0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
                                       0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    void process(const uint8_t *p) {
        uint32_t w[64];

        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(p[i * 4 + 0]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        }

        for (int i = 16; i < 64; ++i)
            w[i] = ssig1(w[i - 2]) + w[i - 7] + ssig0(w[i - 15]) + w[i - 16];

        uint32_t a = h[0];
        uint32_t b = h[1];
        uint32_t c = h[2];
        uint32_t d = h[3];
        uint32_t e = h[4];
        uint32_t f = h[5];
        uint32_t g = h[6];
        uint32_t hh = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + bsig1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = bsig0(a) + maj(a, b, c);

            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;

        std::memset(w, 0, sizeof(w));
    }

  public:
    void update(const void *data, size_t len) {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        bitlen += uint64_t(len) * 8;

        while (len) {
            size_t n = 64 - buflen;
            if (n > len)
                n = len;

            std::memcpy(buf + buflen, p, n);
            buflen += n;
            p += n;
            len -= n;

            if (buflen == 64) {
                process(buf);
                buflen = 0;
            }
        }
    }

    void final(uint8_t out[32]) {
        const uint64_t total_bits = bitlen;

        buf[buflen++] = 0x80;

        if (buflen > 56) {
            std::memset(buf + buflen, 0, 64 - buflen);
            process(buf);
            buflen = 0;
        }

        std::memset(buf + buflen, 0, 56 - buflen);

        for (int i = 0; i < 8; ++i)
            buf[56 + i] = uint8_t(total_bits >> (56 - i * 8));

        process(buf);

        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = uint8_t(h[i] >> 24);
            out[i * 4 + 1] = uint8_t(h[i] >> 16);
            out[i * 4 + 2] = uint8_t(h[i] >> 8);
            out[i * 4 + 3] = uint8_t(h[i]);
        }

        std::memset(buf, 0, sizeof(buf));
        std::memset(h, 0, sizeof(h));
    }
};

inline void hmac_sha256(const void *key, size_t key_len, const void *data1, size_t data1_len, const void *data2, size_t data2_len, uint8_t out[32]) {
    uint8_t key_block[64]{};
    uint8_t khash[32];

    if (key_len > 64) {
        sha256 s;
        s.update(key, key_len);
        s.final(khash);

        std::memcpy(key_block, khash, 32);
        std::memset(khash, 0, sizeof(khash));
    } else {
        std::memcpy(key_block, key, key_len);
    }

    uint8_t ipad[64];
    uint8_t opad[64];

    for (int i = 0; i < 64; ++i) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }

    uint8_t inner[32];

    {
        sha256 s;
        s.update(ipad, 64);

        if (data1_len)
            s.update(data1, data1_len);

        if (data2_len)
            s.update(data2, data2_len);

        s.final(inner);
    }

    {
        sha256 s;
        s.update(opad, 64);
        s.update(inner, 32);
        s.final(out);
    }

    std::memset(key_block, 0, sizeof(key_block));
    std::memset(ipad, 0, sizeof(ipad));
    std::memset(opad, 0, sizeof(opad));
    std::memset(inner, 0, sizeof(inner));
}

} // namespace pbkdf2_detail

inline std::array<uint8_t, 32> pbkdf2_sha256(const void *password, size_t password_len, std::string salt, uint32_t iterations) {
    std::array<uint8_t, 32> result{};

    if (iterations == 0)
        return result;

    const uint8_t block_index[4] = {0, 0, 0, 1};

    uint8_t u[32];
    uint8_t t[32];

    // U1 = PRF(P, S || INT(1))
    pbkdf2_detail::hmac_sha256(password, password_len, salt.c_str(), salt.size(), block_index, sizeof(block_index), u);

    std::memcpy(t, u, sizeof(t));

    // U2 ... Uc
    for (uint32_t i = 1; i < iterations; ++i) {
        uint8_t next[32];

        pbkdf2_detail::hmac_sha256(password, password_len, u, sizeof(u), nullptr, 0, next);

        for (int j = 0; j < 32; ++j)
            t[j] ^= next[j];

        std::memcpy(u, next, sizeof(u));
        std::memset(next, 0, sizeof(next));
    }

    std::memcpy(result.data(), t, 32);

    std::memset(u, 0, sizeof(u));
    std::memset(t, 0, sizeof(t));

    return result;
}

inline void aes256_ctr_xor(uint8_t *data, size_t len, const uint8_t key[32], uint8_t ctr[16]) {
    if (len == 0) {
        return;
    }
    tinyaes_ctr_crypt(key, 32, ctr, data, len, data, len);
}

inline void aes256_encrypt_block(const uint8_t in[16], uint8_t out[16], const uint8_t key[32]) {
	tinyaes_ecb_encrypt(key, 32, in, 16, out, 16);
}

inline void aes256_ctr_xor_with_iv(uint8_t *data, size_t len, const uint8_t key[32], const uint8_t iv12[12], uint64_t data_offset) {
    if (len == 0) {
        return;
    }

    uint8_t ctr[16];
    std::memcpy(ctr, iv12, 12);
    uint64_t block_index = static_cast<uint64_t>(data_offset) / 16ULL;
    uint32_t counter = static_cast<uint32_t>(1 + block_index);
    ctr[12] = static_cast<uint8_t>((counter >> 24) & 0xFF);
    ctr[13] = static_cast<uint8_t>((counter >> 16) & 0xFF);
    ctr[14] = static_cast<uint8_t>((counter >> 8) & 0xFF);
    ctr[15] = static_cast<uint8_t>((counter >> 0) & 0xFF);

    size_t inner = static_cast<size_t>(data_offset % 16);

    if (inner != 0) {
        uint8_t block[16];
        dup_crypto::aes256_encrypt_block(ctr, block, key);

        size_t take = std::min<size_t>(16 - inner, len);
        for (size_t i = 0; i < take; ++i)
            data[i] ^= block[inner + i];

        // increment counter (big-endian)
        for (int j = 15; j >= 0; --j)
            if (++ctr[j])
                break;

        if (len > take)
            dup_crypto::aes256_ctr_xor(data + take, len - take, key, ctr);
    } else {
        dup_crypto::aes256_ctr_xor(data, len, key, ctr);
    }
}


inline std::array<uint8_t,32> derive_key_from_passphrase(const std::string &passphrase, const std::string& salt) {
    auto out = pbkdf2_sha256(passphrase.data(), passphrase.size(), salt, 1000);
	return out;
}


inline void random_bytes(uint8_t *buf, size_t len) {
	std::random_device rd;
	std::mt19937_64 gen(rd());
	for (size_t i = 0; i < len; ++i) buf[i] = static_cast<uint8_t>(gen() & 0xFF);
}

}

