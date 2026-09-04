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
#include "internal/aes_impl.h"
#include "internal/ghash.h"

#include <atomic>

namespace tinyaes
{
    namespace internal
    {

        // --- Encrypt block dispatch ---
        static encrypt_block_fn resolve_encrypt_block();
        static std::atomic<encrypt_block_fn> encrypt_block_impl {nullptr};

        encrypt_block_fn get_encrypt_block()
        {
            auto fn = encrypt_block_impl.load(std::memory_order_acquire);
            if (fn)
                return fn;
            fn = resolve_encrypt_block();
            encrypt_block_impl.store(fn, std::memory_order_release);
            return fn;
        }

        static encrypt_block_fn resolve_encrypt_block()
        {
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(TINYAES_FORCE_PORTABLE)
            auto features = detect_cpu_features();
            if (features.aesni)
                return aes_encrypt_block_aesni;
            return aes_encrypt_block_portable;
#elif (defined(__aarch64__) || defined(_M_ARM64)) && !defined(TINYAES_FORCE_PORTABLE)
            auto features = detect_cpu_features();
            if (features.arm_aes)
                return aes_encrypt_block_arm_ce;
            return aes_encrypt_block_portable;
#else
            return aes_encrypt_block_portable;
#endif
        }

        // --- Decrypt block dispatch ---
        static decrypt_block_fn resolve_decrypt_block();
        static std::atomic<decrypt_block_fn> decrypt_block_impl {nullptr};

        decrypt_block_fn get_decrypt_block()
        {
            auto fn = decrypt_block_impl.load(std::memory_order_acquire);
            if (fn)
                return fn;
            fn = resolve_decrypt_block();
            decrypt_block_impl.store(fn, std::memory_order_release);
            return fn;
        }

        static decrypt_block_fn resolve_decrypt_block()
        {
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(TINYAES_FORCE_PORTABLE)
            auto features = detect_cpu_features();
            if (features.aesni)
                return aes_decrypt_block_aesni;
            return aes_decrypt_block_portable;
#elif (defined(__aarch64__) || defined(_M_ARM64)) && !defined(TINYAES_FORCE_PORTABLE)
            auto features = detect_cpu_features();
            if (features.arm_aes)
                return aes_decrypt_block_arm_ce;
            return aes_decrypt_block_portable;
#else
            return aes_decrypt_block_portable;
#endif
        }

        // --- Key expand dispatch ---
        static key_expand_fn resolve_key_expand();
        static std::atomic<key_expand_fn> key_expand_impl {nullptr};

        key_expand_fn get_key_expand()
        {
            auto fn = key_expand_impl.load(std::memory_order_acquire);
            if (fn)
                return fn;
            fn = resolve_key_expand();
            key_expand_impl.store(fn, std::memory_order_release);
            return fn;
        }

        static key_expand_fn resolve_key_expand()
        {
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(TINYAES_FORCE_PORTABLE)
            auto features = detect_cpu_features();
            if (features.aesni)
                return aes_key_expand_aesni;
            return aes_key_expand_portable;
#else
            return aes_key_expand_portable;
#endif
        }

        // --- CTR pipeline dispatch ---
        static ctr_pipeline_fn resolve_ctr_pipeline();
        static std::atomic<ctr_pipeline_fn> ctr_pipeline_impl {nullptr};

        ctr_pipeline_fn get_ctr_pipeline()
        {
            auto fn = ctr_pipeline_impl.load(std::memory_order_acquire);
            if (fn)
                return fn;
            fn = resolve_ctr_pipeline();
            ctr_pipeline_impl.store(fn, std::memory_order_release);
            return fn;
        }

        static ctr_pipeline_fn resolve_ctr_pipeline()
        {
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(TINYAES_FORCE_PORTABLE)
            auto features = detect_cpu_features();
            if (features.vaes && features.avx512f)
                return aes_ctr_pipeline_vaes;
            if (features.aesni)
                return aes_ctr_pipeline_aesni;
            return aes_ctr_pipeline_portable;
#elif (defined(__aarch64__) || defined(_M_ARM64)) && !defined(TINYAES_FORCE_PORTABLE)
            auto features = detect_cpu_features();
            if (features.arm_aes)
                return aes_ctr_pipeline_arm_ce;
            return aes_ctr_pipeline_portable;
#else
            return aes_ctr_pipeline_portable;
#endif
        }

        // --- GHASH dispatch ---
        static ghash_fn resolve_ghash();
        static std::atomic<ghash_fn> ghash_impl {nullptr};

        ghash_fn get_ghash()
        {
            auto fn = ghash_impl.load(std::memory_order_acquire);
            if (fn)
                return fn;
            fn = resolve_ghash();
            ghash_impl.store(fn, std::memory_order_release);
            return fn;
        }

        static ghash_fn resolve_ghash()
        {
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(TINYAES_FORCE_PORTABLE)
            auto features = detect_cpu_features();
            if (features.vpclmulqdq && features.avx512f)
                return ghash_vpclmulqdq;
            if (features.pclmulqdq)
                return ghash_pclmulqdq;
            return ghash_portable;
#elif (defined(__aarch64__) || defined(_M_ARM64)) && !defined(TINYAES_FORCE_PORTABLE)
            auto features = detect_cpu_features();
            if (features.arm_pmull)
                return ghash_arm_ce;
            return ghash_portable;
#else
            return ghash_portable;
#endif
        }

    } // namespace internal
} // namespace tinyaes
