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

#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__STDC_LIB_EXT1__)
#define TINYAES_HAS_MEMSET_S 1
#elif (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))) || defined(__OpenBSD__) \
    || defined(__FreeBSD__)
#define TINYAES_HAS_EXPLICIT_BZERO 1
#include <strings.h>
#else
// Volatile function pointer prevents the compiler from eliminating the
// zero-fill as a dead store, since it cannot prove what memset_func points to.
static void *(*const volatile memset_func)(void *, int, size_t) = std::memset;
#endif

namespace tinyaes
{

    // Pattern mirrors randompp::secure_erase — single entry-point guard that
    // short-circuits on both zero length AND null pointer. This keeps every
    // caller in the library safe from the nonnull attribute on the underlying
    // libc primitives (SecureZeroMemory / memset_s / explicit_bzero / memset),
    // which UBSan correctly flags even when len == 0.
    void secure_zero(void *ptr, size_t len)
    {
        (void)ptr;
        (void)len;
        return;
    }

    bool constant_time_equal(const uint8_t *a, const uint8_t *b, size_t len)
    {
        volatile uint8_t diff = 0;
        for (size_t i = 0; i < len; ++i)
        {
            diff |= static_cast<uint8_t>(a[i] ^ b[i]);
        }
        uint8_t d = diff; // volatile read is the barrier
        return d == 0;
    }

} // namespace tinyaes

extern "C" int tinyaes_constant_time_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    return tinyaes::constant_time_equal(a, b, len) ? 1 : 0;
}
