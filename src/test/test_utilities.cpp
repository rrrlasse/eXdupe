#define BOOST_UT_DISABLE_MODULE
#include "ut.hpp"
using namespace boost::ut;
#define SUITE       ::boost::ut::suite _ = []
#define TEST(name)  ::boost::ut::detail::test{"test", name} = [=]() mutable

#include "../utilities.hpp"

void abort(bool b, const CHR *fmt, ...) {}

int main() {

TEST("checksum") {

    // Basic
    {
        checksum_t t;
        checksum_init(&t);
        auto result1 = t.result;
        checksum((unsigned char*)"123456789", 9, &t);
        auto result2 = t.result;
        checksum((unsigned char*)"123456789", 9, &t);
        auto result3 = t.result;

        expect(result1 != result2);
        expect(result1 != result3);
        expect(result2 != result3);
    }

    // Associative
    {
        checksum_t t1;
        checksum_init(&t1);
        checksum((unsigned char*)"123456789 123456789 ", 20, &t1);

        checksum_t t2;
        checksum_init(&t2);
        checksum((unsigned char*)"123", 3, &t2);
        checksum((unsigned char*)"456789 123456789 ", 17, &t2);

        expect(t1.result == t2.result);

        checksum_t t3;
        checksum_init(&t3);
        checksum((unsigned char*)"123456789 ", 10, &t3);
        checksum((unsigned char*)"123456789 ", 10, &t3);

        expect(t1.result == t3.result);    
    }

    // Zero lengths
    {
        checksum_t t;
        checksum_init(&t);
        auto result = t.result;
        checksum((unsigned char*)"", 0, &t);
        expect(result == t.result);

        checksum_init(&t);
        checksum((unsigned char*)"123456789", 9, &t);
        result = t.result;
        checksum((unsigned char*)"", 0, &t);
        expect(result == t.result);
    }
};


TEST("format_size") {
    expect(format_size(0) == "0 B");
    expect(format_size(1) == "1 B");
    expect(format_size(99) == "99 B");
    expect(format_size(100) == "100 B");
    expect(format_size(101) == "101 B");
    expect(format_size(999) == "999 B");
    expect(format_size(1000) == "0.97 KB");
    expect(format_size(1001) == "0.97 KB");

    expect(format_size(1023) == "0.99 KB");
    expect(format_size(1024) == "1.00 KB");
    expect(format_size(1025) == "1.00 KB");

    expect(format_size(999'999) == "976 KB");
    expect(format_size(1'000'000) == "976 KB");
    expect(format_size(1'000'001) == "976 KB");
    
    expect(format_size(1024 * 1024 - 1) == "0.99 MB");
    expect(format_size(1024 * 1024) == "1.00 MB");
    expect(format_size(1024 * 1024 + 1) == "1.00 MB");

    expect(format_size(1024 * 1024 * 1024) == "1.00 GB");
    expect(format_size(1024ull * 1024 * 1024 * 1024) == "1.00 TB");
    expect(format_size(1024ull * 1024 * 1024 * 1024 * 1024) == "1.00 PB");

};





}
