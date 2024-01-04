// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#ifndef UTILITIES_HEADER
#define UTILITIES_HEADER

#include "unicode.h"

#define NOMINMAX

// Do not increase too much because this amount is saved on stack in several
// places
#define MAX_PATH_LEN 2048

#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64)
#define WINDOWS
#endif

#if (defined(__X86__) || defined(__i386__) || defined(i386) || defined(_M_IX86) || defined(__386__) || defined(__x86_64__) || defined(_M_X64))
#define X86X64
#endif

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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#ifdef WINDOWS
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#endif

using namespace std; // fixme, remove

enum { FILE_TYPE, DIR_TYPE, SYMLINK_TYPE, ERROR_TYPE };
enum status_t { BACKUP, DIFF_BACKUP, RESTORE, DIFF_RESTORE };

std::string format_size(uint64_t size);
void clear_line();

uint64_t rnd64();

STRING string2wstring(string str);
string wstring2string(STRING wstr);

void myReplaceSTR(std::string &str, const std::string &oldStr, const std::string &newStr);
void myReplace(std::STRING &str, const std::STRING &oldStr, const std::STRING &newStr);
STRING replace2(STRING orig, STRING src, STRING dst);
void cur_date(tm *tm_date);
bool is_symlink(STRING file);
bool is_named_pipe(STRING file);
void print_date(tm *file_date);
void set_date(STRING file, tm *tm_date);
void get_date(STRING file, tm *tm_date);
STRING slashify(STRING path);
STRING slashify(STRING path);
vector<STRING> split_string(STRING str, STRING delim);
int delete_directory(STRING base_dir);
STRING ucase(STRING str);
STRING lcase(STRING str);
STRING remove_leading_curdir(STRING path);
STRING remove_delimitor(STRING path);
STRING remove_leading_delimitor(STRING path);
void abort(bool b, const CHR *fmt, ...);
STRING get_pid(void);
uint64_t filesize(STRING file, bool followlinks);

STRING s2w(const std::string &s);
STRING left(const STRING &s);
STRING right(const STRING &s);

bool ISNAMEDPIPE(int attributes);
bool ISDIR(int attributes);
bool ISLINK(int attributes);

int get_attributes(STRING path, bool follow);
bool set_attributes(STRING path, int attributes);

bool create_directory(STRING path);
bool create_directories(STRING path);
size_t longest_common_prefix(vector<STRING> strings, bool case_sensitive);

template <class T, class U> const uint64_t minimum(const T a, const U b) {
    return (static_cast<uint64_t>(a) > static_cast<uint64_t>(b)) ? static_cast<uint64_t>(b) : static_cast<uint64_t>(a);
}

typedef struct {
    uint64_t remainder;
    uint64_t remainder_len;
    uint64_t b_val;
    uint64_t a_val;
    uint64_t result;
} checksum_t;

void checksum(unsigned char *data, size_t len, checksum_t *t);
void checksum_init(checksum_t *t);
STRING abs_path(STRING source);
bool exists(STRING file);
bool is_dir(STRING path);
STRING str(uint64_t l);

#ifndef WINDOWS
unsigned int GetTickCount();
#endif

std::STRING del(int64_t l, size_t width = 0);
bool equal2(const void *src1, const void *src2, size_t len);
bool same2(CHR *src, size_t len);
void *tmalloc(size_t size);
void set_bold(bool bold);

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

#endif
