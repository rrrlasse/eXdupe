#define BOOST_UT_DISABLE_MODULE
#include "ut.hpp"
using namespace boost::ut;
#define SUITE       ::boost::ut::suite _ = []
#define TEST(name)  ::boost::ut::detail::test{"test", name} = [=]() mutable

#include "../utilities.hpp"

void abort(bool b, const CHR *fmt, ...) {}

int main() {

TEST("checksum_basic") {
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
};

TEST("checksum_associative") {
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
};

TEST("checksum_zero_lengths") {
    {
        checksum_t t;
        checksum_init(&t);
        auto result1 = t.result;
        checksum((unsigned char*)"", 0, &t);
        expect(result1 == t.result);
    }
    {
        checksum_t t;
        checksum_init(&t);
        checksum((unsigned char*)"123456789", 9, &t);
        auto result1 = t.result;
        checksum((unsigned char*)"", 0, &t);
        expect(result1 == t.result);
    }
};

}
