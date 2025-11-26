// SPDX-License-Identifier: MIT
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2025: Lasse Mikkel Reinhold

#pragma once

#include "unicode.h"
#include "error_handling.h"

#define NOMINMAX

// Do not increase too much because this amount is saved on stack in several
// places
#define MAX_PATH_LEN 2048

#include <string.h>
#include <string>
#include <vector>
#if defined(__SVR4) && defined(__sun)
#include <thread.h>
#endif

#if defined(hpux) || defined(__hpux)
#include <unistd.h>
#endif
#include <algorithm>
#include <ctime>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <tuple>
#include <source_location>
#include <iostream>
#include <array>
#include <utility>
#ifdef _WIN32
#define CASESENSE(str) lcase(str)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#define CASESENSE(str) str
#endif

#include "libexdupe/gxhash/gxhash.h"

enum { FILE_TYPE, DIR_TYPE, SYMLINK_TYPE, ERROR_TYPE };
enum status_t { BACKUP, DIFF_BACKUP, RESTORE, DIFF_RESTORE, LIST, DIFF_LIST };

// milliseconds since epoch
typedef long long time_ms_t; 

std::tm local_time_tm(const time_ms_t &t);
std::string suffix(uint64_t size, bool column = false);
uint64_t rnd64();
bool is_valid_utf8(const std::string& input) ;
void replace_stdstr(std::string &str, const std::string &oldStr, const std::string &newStr);
void replace_str(STRING &str, const STRING &oldStr, const STRING &newStr);
time_ms_t cur_date();
bool is_symlink(const STRING& file);
bool symlink_target(const CHR *symbolicLinkPath, STRING &targetPath, bool &is_dir);
bool is_named_pipe(const STRING& file);
bool set_date(const STRING& file, time_ms_t date);
std::pair<time_ms_t, time_ms_t> get_date(const STRING& file);
STRING slashify(STRING path);
std::vector<STRING> split_string(STRING str, STRING delim);
int delete_directory(const STRING& base_dir);
STRING ucase(STRING str);
STRING lcase(STRING str);
STRING remove_leading_curdir(const STRING& path);
STRING remove_delimitor(const STRING& path);
STRING remove_leading_delimitor(STRING path);
uint64_t filesize(const STRING& file, bool followlinks);
bool same_path(const STRING& p1, STRING p2);

STRING s2w(const std::string &s);
string w2s(const STRING &s);
STRING left(const STRING &s);
STRING right(const STRING &s);

bool ISNAMEDPIPE(int attributes);
bool ISDIR(int attributes);
bool ISLINK(int attributes);
bool ISREG(int attributes);
bool ISSOCK(int attributes);

int get_attributes(STRING path, bool follow);
bool set_attributes(const STRING& path, int attributes);

bool create_directory(const STRING& path);
bool create_directories(const STRING& path, time_ms_t t);
size_t longest_common_prefix(const std::vector<STRING>& strings, bool case_sensitive);

template <class T, class U> uint64_t minimum(T a, U b) {
    return (static_cast<uint64_t>(a) > static_cast<uint64_t>(b)) ? static_cast<uint64_t>(b) : static_cast<uint64_t>(a);
}

struct checksum_t {
    gxhash_state state{};
    gxhash_register hash{};
    std::array<char, 16> result();
    uint64_t result64();
    uint32_t hash_seed{};
    bool use_aesni;
};

void checksum(const char *data, size_t len, checksum_t *t);
void checksum_init(checksum_t *t, uint32_t hash_seed, bool use_aesni);
STRING abs_path(const STRING& source);
bool exists(const STRING& file);
bool is_dir(const STRING& path);

#ifndef _WIN32
uint64_t GetTickCount64();
#endif

STRING del(int64_t l, size_t width = 0);
void *tmalloc(size_t size);
void set_bold(bool bold);

#ifdef _WIN32
bool is_symlink_consistent(const std::wstring &symlinkPath);
#endif

typedef struct {
    short int tm_year;      /* years since 1970 */
    unsigned char tm_sec;   /* seconds after the minute - [0,59] */
    unsigned char tm_min;   /* minutes after the hour - [0,59] */
    unsigned char tm_hour;  /* hours since midnight - [0,23] */
    unsigned char tm_mday;  /* day of the month - [1,31] */
    unsigned char tm_mon;   /* months since January - [0,11] */
    unsigned char tm_wday;  /* days since Sunday - [0,6] */
    unsigned char tm_yday;  /* days since January 1 - [0,365] */
    unsigned char tm_isdst; /* daylight savings time flag */
} short_tm;

void tm_to_short(short_tm *s, tm *l);
void tm_to_long(short_tm *s, tm *l);

std::string regx(std::string str, std::string pat);

template <class A, class B> class scope_actions {
  public:
    explicit scope_actions(const A &aa, const B &bb) noexcept : b{bb} { aa(); }
    explicit scope_actions(const A aa, const B &bb) noexcept : b{bb} { aa(); }
    explicit scope_actions(A &&aa, B &&bb) noexcept : b{std::move(bb)} { aa(); }

    ~scope_actions() noexcept {
        if (invoke)
            b();
    }

    scope_actions(scope_actions &&other) noexcept : b(std::move(other.b)), invoke(std::exchange(other.invoke, false)) {}

    scope_actions(const scope_actions &) = delete;
    void operator=(const scope_actions &) = delete;
    void operator=(scope_actions &&) = delete;

  private:
    B b;
    bool invoke = true;
};