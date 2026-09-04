// Copyright (c) 2025-2026, Brandon Lehmann
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "tinyaes/common.h"

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#elif defined(__linux__)
#include <sys/random.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <stdlib.h>
#endif

namespace tinyaes
{

    int generate_iv(uint8_t *out, size_t len)
    {
        if (!out || len == 0)
            return -1;

#if defined(_WIN32)
        NTSTATUS status = BCryptGenRandom(nullptr, out, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return BCRYPT_SUCCESS(status) ? 0 : -1;
#elif defined(__linux__)
        ssize_t ret = getrandom(out, len, 0);
        return (ret == static_cast<ssize_t>(len)) ? 0 : -1;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
        arc4random_buf(out, len);
        return 0;
#else
#include <cstdio>
        // Fallback: read from /dev/urandom
        FILE *f = fopen("/dev/urandom", "rb");
        if (!f)
            return -1;
        size_t n = fread(out, 1, len, f);
        fclose(f);
        return (n == len) ? 0 : -1;
#endif
    }

    std::vector<uint8_t> generate_iv()
    {
        std::vector<uint8_t> iv(16);
        if (generate_iv(iv.data(), 16) != 0)
            iv.clear();
        return iv;
    }

    std::vector<uint8_t> generate_nonce()
    {
        std::vector<uint8_t> nonce(12);
        if (generate_iv(nonce.data(), 12) != 0)
            nonce.clear();
        return nonce;
    }

} // namespace tinyaes

extern "C" int tinyaes_generate_iv(uint8_t out[16])
{
    return tinyaes::generate_iv(out, 16);
}

extern "C" int tinyaes_generate_nonce(uint8_t out[12])
{
    return tinyaes::generate_iv(out, 12);
}
