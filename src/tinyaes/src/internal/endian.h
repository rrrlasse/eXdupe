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

#pragma once

#include <cstdint>

namespace tinyaes
{
    namespace internal
    {

        inline uint32_t load_be32(const uint8_t *p)
        {
            return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
                   | (static_cast<uint32_t>(p[2]) << 8) | (static_cast<uint32_t>(p[3]));
        }

        inline uint64_t load_be64(const uint8_t *p)
        {
            return (static_cast<uint64_t>(p[0]) << 56) | (static_cast<uint64_t>(p[1]) << 48)
                   | (static_cast<uint64_t>(p[2]) << 40) | (static_cast<uint64_t>(p[3]) << 32)
                   | (static_cast<uint64_t>(p[4]) << 24) | (static_cast<uint64_t>(p[5]) << 16)
                   | (static_cast<uint64_t>(p[6]) << 8) | (static_cast<uint64_t>(p[7]));
        }

        inline void store_be32(uint8_t *p, uint32_t v)
        {
            p[0] = static_cast<uint8_t>(v >> 24);
            p[1] = static_cast<uint8_t>(v >> 16);
            p[2] = static_cast<uint8_t>(v >> 8);
            p[3] = static_cast<uint8_t>(v);
        }

        inline void store_be64(uint8_t *p, uint64_t v)
        {
            p[0] = static_cast<uint8_t>(v >> 56);
            p[1] = static_cast<uint8_t>(v >> 48);
            p[2] = static_cast<uint8_t>(v >> 40);
            p[3] = static_cast<uint8_t>(v >> 32);
            p[4] = static_cast<uint8_t>(v >> 24);
            p[5] = static_cast<uint8_t>(v >> 16);
            p[6] = static_cast<uint8_t>(v >> 8);
            p[7] = static_cast<uint8_t>(v);
        }

        // Increment big-endian 32-bit counter in the last 4 bytes of a 16-byte block
        inline void increment_be32(uint8_t block[16])
        {
            uint32_t ctr = load_be32(block + 12);
            ++ctr;
            store_be32(block + 12, ctr);
        }

    } // namespace internal
} // namespace tinyaes
