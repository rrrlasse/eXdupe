#include <chrono>
#include <thread>

#define CATCH_CONFIG_RUNNER
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "../utilities.hpp"
#include "../ui.hpp"

void abort(bool b, const CHR *fmt, ...) {}

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


TEST_CASE("checksum") {
    {   
        // Basic
        checksum_t t;
        checksum_init(&t);
        auto result1 = t.result;
        checksum((unsigned char *)"123456789", 9, &t);
        auto result2 = t.result;
        checksum((unsigned char *)"123456789", 9, &t);
        auto result3 = t.result;

        REQUIRE(result1 != result2);
        REQUIRE(result1 != result3);
        REQUIRE(result2 != result3);
    }

    {
        // Associative
        checksum_t t1;
        checksum_init(&t1);
        checksum((unsigned char *)"123456789 123456789 ", 20, &t1);

        checksum_t t2;
        checksum_init(&t2);
        checksum((unsigned char *)"123", 3, &t2);
        checksum((unsigned char *)"456789 123456789 ", 17, &t2);

        REQUIRE(t1.result == t2.result);

        checksum_t t3;
        checksum_init(&t3);
        checksum((unsigned char *)"123456789 ", 10, &t3);
        checksum((unsigned char *)"123456789 ", 10, &t3);

        REQUIRE(t1.result == t3.result);
    }

    {
        // Zero lengths
        checksum_t t;
        checksum_init(&t);
        auto result = t.result;
        checksum((unsigned char *)"", 0, &t);
        REQUIRE(result == t.result);

        checksum_init(&t);
        checksum((unsigned char *)"123456789", 9, &t);
        result = t.result;
        checksum((unsigned char *)"", 0, &t);
        REQUIRE(result == t.result);
    }
}

TEST_CASE("format_size") {
    REQUIRE(format_size(0) == "0 ");
    REQUIRE(format_size(1) == "1 ");
    REQUIRE(format_size(99) == "99 ");
    REQUIRE(format_size(100) == "100 ");
    REQUIRE(format_size(101) == "101 ");
    REQUIRE(format_size(999) == "999 ");
    REQUIRE(format_size(1000) == "0.97 K");
    REQUIRE(format_size(1001) == "0.97 K");

    REQUIRE(format_size(1023) == "0.99 K");
    REQUIRE(format_size(1024) == "1.00 K");
    REQUIRE(format_size(1025) == "1.00 K");

    REQUIRE(format_size(999'999) == "976 K");
    REQUIRE(format_size(1'000'000) == "976 K");
    REQUIRE(format_size(1'000'001) == "976 K");

    REQUIRE(format_size(1024 * 1024 - 1) == "0.99 M");
    REQUIRE(format_size(1024 * 1024) == "1.00 M");
    REQUIRE(format_size(1024 * 1024 + 1) == "1.00 M");

    REQUIRE(format_size(1024 * 1024 * 1024) == "1.00 G");
    REQUIRE(format_size(1024ull * 1024 * 1024 * 1024) == "1.00 T");
    REQUIRE(format_size(1024ull * 1024 * 1024 * 1024 * 1024) == "1.00 P");
}


#ifdef _WIN32 // because of paths

TEST_CASE("ui") {
    std::wostringstream oss;

    auto create = [&]() {
        oss.clear();
        oss.str(STRING());
        Statusbar s(oss);
        s.m_term_width = 30;
        s.m_verbose_level = 1;
        s.m_base_dir = UNITXT("d:\\");
        return s;
    };

    {
        // Truncate filename
        auto s = create();
        s.update(BACKUP, 0, 0, UNITXT("d:\\12345678901234567890123"));
        STRING res = term(oss.str());
        REQUIRE(res == UNITXT("0 B, 0 B, 123456789012345678.."));
    }

    {
        // Filename accurately fits, no truncation
        auto s = create();
        s.update(BACKUP, 0, 0, UNITXT("d:\\12345678901234567890"));
        STRING res = term(oss.str());
        REQUIRE(res == UNITXT("0 B, 0 B, 12345678901234567890"));
    }

    {
        // Filename shorther, show all
        auto s = create();
        s.update(BACKUP, 0, 0, UNITXT("d:\\123"));
        STRING res = term(oss.str());
        REQUIRE(res == UNITXT("0 B, 0 B, 123"));
    }

    {
        // Show KB in right order
        auto s = create();
        s.update(BACKUP, 1024, 2048, UNITXT("d:\\123"));
        STRING res = term(oss.str());
        REQUIRE(res == UNITXT("1.00 KB, 2.00 KB, 123"));
    }

    {
        // Restore test
        auto s = create();
        s.update(RESTORE, 0, 1024, UNITXT("d:\\123"));
        STRING res = term(oss.str());
        REQUIRE(res == UNITXT("1.00 KB, 123"));
    }

    {
        // Print long line, then short line. No artifacts must be present at line end
        auto s = create();
        s.update(BACKUP, 0, 0, UNITXT("d:\\1234567890"));
        s.update(BACKUP, 0, 0, UNITXT("d:\\1"), true);
        auto res = term(oss.str());
        REQUIRE(res == UNITXT("0 B, 0 B, 1"));
    }

    {
        // Empty base_dir (files/dirs passed on the command line had no common prefix)
        auto s = create();
        s.m_base_dir = UNITXT("");
        s.update(BACKUP, 0, 0, UNITXT("d:\\123"));
        STRING res = term(oss.str());
        REQUIRE(res == UNITXT("0 B, 0 B, d:\\123"));
    }

    {
        // Verbosity 0
        auto s = create();
        s.m_verbose_level = 0;
        s.update(BACKUP, 0, 0, UNITXT("d:\\123"));
        STRING res = term(oss.str());
        REQUIRE(res == UNITXT(""));
    }

    {
        // Verbosity 3: never truncate, absolute paths, show all files
        auto s = create();
        s.m_verbose_level = 3;
        s.update(BACKUP, 0, 0, UNITXT("d:\\1234567890123456789012345678901234567890"));
        s.update(BACKUP, 0, 0, UNITXT("d:\\456"));
        STRING res = term(oss.str());
        REQUIRE(res == UNITXT("  d:\\1234567890123456789012345678901234567890\n  d:\\456\n"));
    }
}

#endif