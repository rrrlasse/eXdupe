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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace test
{

    struct TestCase
    {
        std::string name;
        std::function<void()> fn;
    };

    inline int &fail_count()
    {
        static int n = 0;
        return n;
    }
    inline int &pass_count()
    {
        static int n = 0;
        return n;
    }

    inline std::vector<TestCase> &test_registry()
    {
        static std::vector<TestCase> cases;
        return cases;
    }

    struct TestRegistrar
    {
        TestRegistrar(const char *name, std::function<void()> fn)
        {
            test_registry().push_back({name, std::move(fn)});
        }
    };

    inline void
        assert_eq_bytes(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, const char *file, int line)
    {
        // Guard: std::memcmp's pointer args are declared nonnull, so passing
        // an empty vector's data() (which may return nullptr) is UB even when
        // n == 0. Compare sizes first and short-circuit the memcmp on empty.
        const bool equal = (a.size() == b.size()) && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
        if (!equal)
        {
            std::fprintf(stderr, "%s:%d: ASSERT_EQ failed\n  got:      ", file, line);
            for (auto x : a)
                std::fprintf(stderr, "%02x", static_cast<unsigned>(x));
            std::fprintf(stderr, "\n  expected: ");
            for (auto x : b)
                std::fprintf(stderr, "%02x", static_cast<unsigned>(x));
            std::fprintf(stderr, "\n");
            fail_count()++;
        }
        else
        {
            pass_count()++;
        }
    }

    inline void assert_true(bool cond, const char *expr, const char *file, int line)
    {
        if (!cond)
        {
            std::fprintf(stderr, "%s:%d: ASSERT_TRUE(%s) failed\n", file, line, expr);
            fail_count()++;
        }
        else
        {
            pass_count()++;
        }
    }

    inline int run_all()
    {
        for (auto &tc : test_registry())
        {
            std::printf("  RUN  %s\n", tc.name.c_str());
            tc.fn();
        }
        std::printf("\n%d passed, %d failed\n", pass_count(), fail_count());
        return fail_count() > 0 ? 1 : 0;
    }

} // namespace test

#define TEST(name)                                             \
    static void test_##name();                                 \
    static test::TestRegistrar reg_##name(#name, test_##name); \
    static void test_##name()

#define ASSERT_EQ(a, b) test::assert_eq_bytes((a), (b), __FILE__, __LINE__)
#define ASSERT_TRUE(cond) test::assert_true((cond), #cond, __FILE__, __LINE__)

#define RUN_ALL_TESTS() test::run_all()
