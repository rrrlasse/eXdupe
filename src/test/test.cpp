#include <chrono>
#include <thread>

#define CATCH_CONFIG_RUNNER
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "../error_handling.h"
#include "../utilities.hpp"
#include "../ui.hpp"
#include "../bytebuffer.h"

struct PrintTestNameListener : Catch::TestEventListenerBase {
    using TestEventListenerBase::TestEventListenerBase;

    void testCaseStarting(const Catch::TestCaseInfo &testInfo) override { std::cout << ">>> Running test: " << testInfo.name << std::endl; }
};

CATCH_REGISTER_LISTENER(PrintTestNameListener)

void unshadow() {};

using std::wstring;

// Simulate a terminal and compute what would be seen on the screen
std::wstring term(const std::wstring& source) {
    wstring destination;
    wstring destline;
    size_t p = 0;    
    auto trim = [](std::wstring input) {
        size_t nonspace = input.find_last_not_of(L" ");
        return nonspace != std::wstring::npos ? input.substr(0, nonspace + 1) : L"";
    };
    for(wchar_t c : source) {
        if(c == '\n') {
            destination += trim(destline) + L'\n';
            destline.clear();
            p = 0;
        }
        else if(c == '\r') {
            p = 0;            
        }
        else {
            if(destline.size() < p + 1) {
                destline += L' ';
            }
            destline[p] = c;
            p++;
        }
    }
    destination += trim(destline);
    return destination;
}

TEST_CASE("bytebuffer") {

    Bytebuffer buf(10);

    auto add = [&](std::string s, uint64_t off, size_t len) {
        buf.buffer_add((const char*)s.c_str(), off, len); 
    };

    
    add("aaa", 10, 3);
    add("bbb", 20, 3);
    add("ccc", 30, 3);
    
    auto a = buf.buffer_find(10, 3);
    auto b = buf.buffer_find(20, 3);
    auto c = buf.buffer_find(30, 3);

    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);

    add("ddd", 40, 3);

    a = buf.buffer_find(10, 3);
    b = buf.buffer_find(20, 3);
    c = buf.buffer_find(30, 3);
    auto d = buf.buffer_find(40, 3);

    REQUIRE(!a);
    REQUIRE(b);
    REQUIRE(c);
    REQUIRE(d);

    if (b) {
        REQUIRE(string(b, 3) == "bbb");
    }
    if (c) {
        REQUIRE(string(c, 3) == "ccc");
    }
    if (d) {
        REQUIRE(string(d, 3) == "ddd");
    }
}

TEST_CASE("hash basic") {
    bool use_aesni = false;
    {
        // Differing
        checksum_t t1;
        checksum_t t2;
        checksum_init(&t1, 0, use_aesni);
        checksum_init(&t2, 0, use_aesni);

        checksum((char *)"AAAAAAAAA", 9, &t1);
        auto result1 = t1.result64();

        checksum((char *)"BBBBBBBBB", 9, &t2);
        auto result2 = t2.result64();

        REQUIRE(result1 != result2);
    }

    {
        // Associative
        checksum_t t1;
        checksum_init(&t1, 0, use_aesni);

        checksum_t t2;
        checksum_init(&t2, 0, use_aesni);

        string one;
        for (int i = 0; i < 32 * 1; i++) {
            one += "12345678";
        }

        string two = one + one;

        checksum(one.c_str(), one.size(), &t1);
        checksum(two.c_str(), two.size(), &t1);

        checksum(two.c_str(), two.size(), &t2);
        checksum(one.c_str(), one.size(), &t2);

        REQUIRE(t1.result64() == t2.result64());
    }

    // Seed
    {
        checksum_t t1;
        checksum_t t2;
        checksum_init(&t1, 1, use_aesni);
        checksum_init(&t2, 2, use_aesni);

        checksum((char *)"AAAAAAAAA", 9, &t1);
        auto result1 = t1.result64();

        checksum((char *)"AAAAAAAAA", 9, &t2);
        auto result2 = t2.result64();

        REQUIRE(result1 != result2);
    }

}

TEST_CASE("hash aes-ni emulation") {

    std::string buf(8*1024, ' ');
    
    for (size_t i = 0; i < buf.size(); i++) {
        checksum_t t1;
        checksum_t t2;
        checksum_init(&t1, 0, true);
        checksum_init(&t2, 0, false);

        checksum((char *)buf.c_str(), i, &t1);
        auto result1 = t1.result64();

        checksum((char *)buf.c_str(), i, &t2);
        auto result2 = t2.result64();

        REQUIRE(result1 == result2);
    }
}

TEST_CASE("format_size") {
    REQUIRE(suffix(0) == "0 ");
    REQUIRE(suffix(1) == "1 ");
    REQUIRE(suffix(99) == "99 ");
    REQUIRE(suffix(100) == "100 ");
    REQUIRE(suffix(101) == "101 ");
    REQUIRE(suffix(999) == "999 ");
    REQUIRE(suffix(1000) == "0.97 K");
    REQUIRE(suffix(1001) == "0.97 K");

    REQUIRE(suffix(1023) == "0.99 K");
    REQUIRE(suffix(1024) == "1.00 K");
    REQUIRE(suffix(1025) == "1.00 K");

    REQUIRE(suffix(999'999) == "976 K");
    REQUIRE(suffix(1'000'000) == "976 K");
    REQUIRE(suffix(1'000'001) == "976 K");

    REQUIRE(suffix(1024 * 1024 - 1) == "0.99 M");
    REQUIRE(suffix(1024 * 1024) == "1.00 M");
    REQUIRE(suffix(1024 * 1024 + 1) == "1.00 M");

    REQUIRE(suffix(1024 * 1024 * 1024) == "1.00 G");
    REQUIRE(suffix(1024ull * 1024 * 1024 * 1024) == "1.00 T");
    REQUIRE(suffix(1024ull * 1024 * 1024 * 1024 * 1024) == "1.00 P");
}


#ifdef _WIN32 // because of paths

/*
TEST_CASE("ui") {
    std::wostringstream oss;

    auto create = [&]() {
        oss.clear();
        oss.str(STRING());
        Statusbar s(oss);
        s.m_term_width = 30;
        s.m_verbose_level = 1;
        s.m_base_dir = L("d:\\");
        return s;
    };

    {
        // Truncate filename
        auto s = create();
        s.update(BACKUP, 0, 0, L("d:\\12345678901234567890123"));
        STRING res = term(oss.str());
        REQUIRE(res == L("0 B, 0 B, 123456789012345678.."));
    }

    {
        // Filename accurately fits, no truncation
        auto s = create();
        s.update(BACKUP, 0, 0, L("d:\\12345678901234567890"));
        STRING res = term(oss.str());
        REQUIRE(res == L("0 B, 0 B, 12345678901234567890"));
    }

    {
        // Filename shorther, show all
        auto s = create();
        s.update(BACKUP, 0, 0, L("d:\\123"));
        STRING res = term(oss.str());
        REQUIRE(res == L("0 B, 0 B, 123"));
    }

    {
        // Show KB in right order
        auto s = create();
        s.update(BACKUP, 1024, 2048, L("d:\\123"));
        STRING res = term(oss.str());
        REQUIRE(res == L("1.00 KB, 2.00 KB, 123"));
    }

    {
        // Restore test
        auto s = create();
        s.update(RESTORE, 0, 1024, L("d:\\123"));
        STRING res = term(oss.str());
        REQUIRE(res == L("1.00 KB, 123"));
    }

    {
        // Print long line, then short line. No artifacts must be present at line end
        auto s = create();
        s.update(BACKUP, 0, 0, L("d:\\1234567890"));
        s.update(BACKUP, 0, 0, L("d:\\1"), true);
        auto res = term(oss.str());
        REQUIRE(res == L("0 B, 0 B, 1"));
    }

    {
        // Empty base_dir (files/dirs passed on the command line had no common prefix)
        auto s = create();
        s.m_base_dir = L("");
        s.update(BACKUP, 0, 0, L("d:\\123"));
        STRING res = term(oss.str());
        REQUIRE(res == L("0 B, 0 B, d:\\123"));
    }

    {
        // Verbosity 0
        auto s = create();
        s.m_verbose_level = 0;
        s.update(BACKUP, 0, 0, L("d:\\123"));
        STRING res = term(oss.str());
        REQUIRE(res == L(""));
    }

    {
        // Verbosity 3: never truncate, absolute paths, show all files
        auto s = create();
        s.m_verbose_level = 3;
        s.update(BACKUP, 0, 0, L("d:\\1234567890123456789012345678901234567890"));
        s.update(BACKUP, 0, 0, L("d:\\456"));
        STRING res = term(oss.str());
        REQUIRE(res == L("  d:\\1234567890123456789012345678901234567890\n  d:\\456\n"));
    }
}
*/

#endif