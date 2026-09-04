// Copyright (c) 2025-2026, Brandon Lehmann
// BSD 3-Clause License (see LICENSE)

#include "cpuid.h"
#include "internal/aes_impl.h"
#include "test_harness.h"

TEST(cpuid_detect_no_crash)
{
    // Just verify detection doesn't crash
    auto features = tinyaes::internal::detect_cpu_features();
    (void)features;
    ASSERT_TRUE(true);
}

TEST(cpuid_dispatch_encrypt_block)
{
    // Verify dispatch resolves to a non-null function pointer
    auto fn = tinyaes::internal::get_encrypt_block();
    ASSERT_TRUE(fn != nullptr);
}

TEST(cpuid_dispatch_decrypt_block)
{
    auto fn = tinyaes::internal::get_decrypt_block();
    ASSERT_TRUE(fn != nullptr);
}

TEST(cpuid_dispatch_key_expand)
{
    auto fn = tinyaes::internal::get_key_expand();
    ASSERT_TRUE(fn != nullptr);
}

TEST(cpuid_dispatch_ctr_pipeline)
{
    auto fn = tinyaes::internal::get_ctr_pipeline();
    ASSERT_TRUE(fn != nullptr);
}
