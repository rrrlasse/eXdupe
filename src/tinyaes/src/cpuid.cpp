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

#include "cpuid.h"

#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
#endif

#if defined(__aarch64__) && defined(__linux__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

namespace tinyaes
{
    namespace internal
    {

#if defined(__x86_64__) || defined(_M_X64)

        static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t &eax, uint32_t &ebx, uint32_t &ecx, uint32_t &edx)
        {
#if defined(_MSC_VER) && !defined(__clang__)
            int regs[4];
            __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
            eax = static_cast<uint32_t>(regs[0]);
            ebx = static_cast<uint32_t>(regs[1]);
            ecx = static_cast<uint32_t>(regs[2]);
            edx = static_cast<uint32_t>(regs[3]);
#else
            __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf), "c"(subleaf));
#endif
        }

        CpuFeatures detect_cpu_features()
        {
            CpuFeatures f;
            uint32_t eax, ebx, ecx, edx;

            // Check max leaf
            cpuid(0, 0, eax, ebx, ecx, edx);
            uint32_t max_leaf = eax;

            // Leaf 1: AES-NI and PCLMULQDQ
            if (max_leaf >= 1)
            {
                cpuid(1, 0, eax, ebx, ecx, edx);
                f.aesni = (ecx & (1u << 25)) != 0;
                f.pclmulqdq = (ecx & (1u << 1)) != 0;
            }

            // Leaf 7, subleaf 0: AVX-512F, VAES, VPCLMULQDQ
            if (max_leaf >= 7)
            {
                cpuid(7, 0, eax, ebx, ecx, edx);
                f.avx512f = (ebx & (1u << 16)) != 0;
                f.vaes = (ecx & (1u << 9)) != 0;
                f.vpclmulqdq = (ecx & (1u << 10)) != 0;
            }

            return f;
        }

#elif defined(__aarch64__) || defined(_M_ARM64)

        CpuFeatures detect_cpu_features()
        {
            CpuFeatures f;

#if defined(__APPLE__)
            // Apple Silicon (M1+) always has AES and PMULL extensions
            f.arm_aes = true;
            f.arm_pmull = true;
#elif defined(__linux__)
            unsigned long hwcap = getauxval(AT_HWCAP);
            f.arm_aes = (hwcap & HWCAP_AES) != 0;
            f.arm_pmull = (hwcap & HWCAP_PMULL) != 0;
#elif defined(_M_ARM64)
            // Windows on ARM64: AES CE is baseline
            f.arm_aes = true;
            f.arm_pmull = true;
#endif

            return f;
        }

#else

        // Non-x86, non-ARM64: no SIMD features
        CpuFeatures detect_cpu_features()
        {
            return CpuFeatures {};
        }

#endif

    } // namespace internal
} // namespace tinyaes
