// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2025: Lasse Mikkel Reinhold

#include <cfenv>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <time.h>
#include <assert.h>
#include <tuple>
#include <regex>

#include "unicode.h"
#include "utilities.hpp"
//#include "abort.h"

#include "libexdupe/xxHash/xxh3.h"
#include "libexdupe/xxHash/xxhash.h"

#ifdef _WIN32
#include "Shlwapi.h"
#else
uint64_t GetTickCount64() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return static_cast<uint64_t>((tv.tv_sec * 1000 + tv.tv_usec / 1000));
}

#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#endif

#ifdef _WIN32
#pragma warning(disable : 4267)
#pragma warning(disable : 4244)
#endif

#ifdef _WIN32
#define CURDIR L(".\\")
#define DELIM_STR L("\\")
#define DELIM_CHAR '\\'
#else
#define CURDIR L("./")
#define DELIM_STR L("/")
#define DELIM_CHAR L('/')
#endif

namespace fs = std::filesystem;
using std::vector;
using std::pair;

bool is_valid_utf8(const std::string& input) {
    int continuationBytes = 0;
    for (char ch : input) {
        if ((ch & 0x80) == 0x00) {
            continuationBytes = 0;
        } else if ((ch & 0xE0) == 0xC0) {
            continuationBytes = 1;
        } else if ((ch & 0xF0) == 0xE0) {
            continuationBytes = 2;
        } else if ((ch & 0xF8) == 0xF0) {
            continuationBytes = 3;
        } else if ((ch & 0xC0) == 0x80) {
            if (continuationBytes > 0) {
                continuationBytes--;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return continuationBytes == 0;
}

std::string suffix(uint64_t size, bool column) {
    string ret;

    if (size <= 999) {
        ret = std::to_string(size) + (!column ? " " : "");
        if (column && ret.size() < 6) {
            ret = string(6 - ret.size(), ' ') + ret;
        }
        return ret;
    }

    const char *suffixes[] = {" ", " K", " M", " G", " T", " P" };
    int suffixIndex = 0;

    double sizeInKB = static_cast<double>(size);

    while (sizeInKB >= 1024.0 && suffixIndex < 8) {
        sizeInKB /= 1024.0;
        suffixIndex++;
    }

    if (sizeInKB >= 1000.0) {
        sizeInKB /= 1024.0;
        suffixIndex++;
    }

    std::ostringstream oss;
    const auto prev_round = std::fegetround();
    std::fesetround(FE_DOWNWARD);  

    if (sizeInKB > 999) {
        oss << std::fixed << std::setprecision(0);
    } else if (sizeInKB > 99) {
        oss << std::fixed << std::setprecision(0);
    } else if (sizeInKB > 9.9) {
        oss << std::fixed << std::setprecision(1);
    } else {
        oss << std::fixed << std::setprecision(2);
    }
    oss << sizeInKB << "" << suffixes[suffixIndex];

    std::fesetround(prev_round);
    ret = oss.str();
    if (column && ret.size() < 6) {
        ret = string(6 - ret.size(), ' ') + ret;
    }
    return ret;
}

STRING s2w(const std::string &s) { return STRING(s.begin(), s.end()); }
string w2s(const STRING &s) { return string(s.begin(), s.end()); }

STRING left(const STRING &s) {
    const size_t t = s.find_last_of(L(PATHDELIMS));
    if (t != string::npos) {
        return s.substr(0, t);
    } else {
        return L("");
    }
}

STRING right(const STRING &s) {
    const size_t t = s.find_last_of(L(PATHDELIMS));
    if (t != string::npos) {
        return s.substr(t + 1);
    } else {
        return L("");
    }
}

uint64_t rnd64() {
    std::random_device rd;
    std::mt19937_64 eng(rd());
    std::uniform_int_distribution<uint64_t> distr;
    return distr(eng);
}

void replace_str(STRING &str, const STRING &oldStr, const STRING &newStr) {
    size_t pos = 0;
    while ((pos = str.find(oldStr, pos)) != STRING::npos) {
        str.replace(pos, oldStr.length(), newStr);
        pos += newStr.length();
    }
}

void replace_stdstr(std::string &str, const std::string &oldStr, const std::string &newStr) {
    size_t pos = 0;
    while ((pos = str.find(oldStr, pos)) != STRING::npos) {
        str.replace(pos, oldStr.length(), newStr);
        pos += newStr.length();
    }
}

bool set_date(const STRING &file, time_ms_t date) {
    time_t t = date / 1000;
    tm *timeInfo = gmtime(&t);
#ifdef _WIN32
    FILETIME ft;

    // Fill SYSTEMTIME structure
    SYSTEMTIME systemTime;
    systemTime.wYear = timeInfo->tm_year + 1900;
    systemTime.wMonth = timeInfo->tm_mon + 1;
    systemTime.wDayOfWeek = timeInfo->tm_wday;
    systemTime.wDay = timeInfo->tm_mday;
    systemTime.wHour = timeInfo->tm_hour;
    systemTime.wMinute = timeInfo->tm_min;
    systemTime.wSecond = timeInfo->tm_sec;
    systemTime.wMilliseconds = 0;

    bool b = (bool)SystemTimeToFileTime(&systemTime, &ft);
    if (!b) {
        return false;
    }

    HANDLE hFile;
    auto abspath = abs_path(file);
    hFile = CreateFileW(abspath.c_str(), FILE_GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    b = (bool)SetFileTime(hFile, &ft, &ft, &ft);
    CloseHandle(hFile);
    return b;

#else
    if(fs::is_symlink(file)) {
        struct timespec times[2];
        times[0].tv_sec = times[1].tv_sec = t;
        times[0].tv_nsec = times[1].tv_nsec = 0;
        int r = utimensat(AT_FDCWD, file.c_str(), times, AT_SYMLINK_NOFOLLOW);
        return r == 0;
    }
    else {
        struct utimbuf Time;
        timeInfo->tm_year -= 1900;
        Time.actime = t;
        Time.modtime = t;
        int r = utime(file.c_str(), &Time);
        return r == 0;
    }

#endif
}

bool is_symlink(const STRING& file) { return ISLINK(get_attributes(file, false)); }

bool is_named_pipe(const STRING& file) { return ISNAMEDPIPE(get_attributes(file, false)); }

bool symlink_target(const CHR *symbolicLinkPath, STRING &targetPath, bool &is_dir) {
#ifdef _WIN32
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile(symbolicLinkPath, &findFileData);
    if (hFind != INVALID_HANDLE_VALUE) {
        FindClose(hFind);
    } else {
        return false;
    }
    is_dir = findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
    try {
        // throws for weird items in AppData\Local\Microsoft\WindowsApps
        targetPath = std::filesystem::read_symlink(symbolicLinkPath);
    }
    catch(std::exception&) {
        return false;
    }

    return true;
#else
#if 1
    // std does not work on Windows if the symlink points to a non-existant directory
    try {
        targetPath = std::filesystem::read_symlink(symbolicLinkPath);
        is_dir = std::filesystem::is_directory(symbolicLinkPath);
    } catch (...) {
        return false;
    }
    return true;
#else
    // posix way
    struct stat fileStat;
    if (lstat(symbolicLinkPath, &fileStat) == -1) {
        return false;
    }
    is_dir ft = S_ISLNK(fileStat.st_mode) && S_ISDIR(fileStat.st_mode);
    return true;
#endif
#endif
}

time_ms_t cur_date() {
    auto current = std::chrono::system_clock::now();
    auto since_epoch = std::chrono::time_point_cast<std::chrono::milliseconds>(current).time_since_epoch();
    return since_epoch.count();
}

std::tm local_time_tm(const time_ms_t &t) {
    std::tm localTime;
    time_t t_s = t / 1000;
#if defined(_MSC_VER) || defined(__MINGW32__)
    localtime_s(&localTime, &t_s);
#else
    std::tm *tmp = localtime(&t_s);
    if (tmp)
        localTime = *tmp;
#endif

    return localTime;
}

// Returns {created time, modified time} on Windows and {status change time, modified time} on nix
pair<time_ms_t, time_ms_t> get_date(const STRING &file) {
#ifdef _WIN32
    ULARGE_INTEGER modified;
    ULARGE_INTEGER created;
    HANDLE hFile = CreateFile(file.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        FILETIME fileTimeModified;
        FILETIME fileTimeCreated;
        if (GetFileTime(hFile, &fileTimeCreated, nullptr, &fileTimeModified)) {
            created.LowPart = fileTimeCreated.dwLowDateTime;
            created.HighPart = fileTimeCreated.dwHighDateTime;
            modified.LowPart = fileTimeModified.dwLowDateTime;
            modified.HighPart = fileTimeModified.dwHighDateTime;
        } else {
            CloseHandle(hFile);
            return {};
        }
        CloseHandle(hFile);
    } else {
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(file.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE) {
            return {};
        }
        created.LowPart = findData.ftCreationTime.dwLowDateTime;
        created.HighPart = findData.ftCreationTime.dwHighDateTime;
        modified.LowPart = findData.ftLastWriteTime.dwLowDateTime;
        modified.HighPart = findData.ftLastWriteTime.dwHighDateTime;
        FindClose(hFind);
    }
    return {static_cast<time_ms_t>((created.QuadPart - 116444736000000000ull) / 10000ull), static_cast<time_ms_t>((modified.QuadPart - 116444736000000000ull) / 10000ull)};
#else
    struct stat attrib;

    if (is_symlink(file)) {
        lstat(file.c_str(), &attrib);
    } else {
        stat(file.c_str(), &attrib);
    }
    return {attrib.st_ctime * 1000, attrib.st_mtime * 1000};
#endif
}

STRING abs_path(const STRING& source) {
    CHR destination[5000];
    CHR *r;
#ifdef _WIN32
    r = _wfullpath(destination, source.c_str(), 5000);
    return r == 0 ? L("") : destination;
#else
    if (fs::is_symlink(source)) {
        fs::path p(source);
        auto parent = p.parent_path();
        if (parent == "") {
            parent = ".";
        }

        fs::path absolutePath = fs::canonical(parent);
        auto res = absolutePath / p.filename();
        return res;
    }

    r = realpath(source.c_str(), destination);
    return r == 0 ? L("") : destination;
#endif
}

STRING slashify(STRING path) {
#ifdef _WIN32
    replace(path.begin(), path.end(), '/', '\\');
    return path;
#else
    //replace(path.begin(), path.end(), '\\', '/');
    return path;
#endif
}

STRING ucase(STRING str) {
    // change each element of the STRING to upper case
    for (unsigned int i = 0; i < str.length(); i++) {
        str[i] = (char)toupper(str[i]);
    }
    return str;
}

STRING lcase(STRING str) {
    // change each element of the STRING to lower case
    STRING s = str;
    for (unsigned int i = 0; i < s.length(); i++) {
        s[i] = (CHR)tolower(s[i]);
    }
    return s;
}

/* reverse:  reverse STRING s in place */
void reverse(char s[]) {
    size_t j;
    unsigned char c;
    unsigned int i;

    for (i = 0, j = strlen(s) - 1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* itoa:  convert n to characters in s */
void itoa(int n, char s[]) {
    int i, sign;

    if ((sign = n) < 0) { /* record sign */
        n = -n;           /* make n positive */
    }
    i = 0;
    do {                       /* generate digits in reverse order */
        s[i++] = n % 10 + '0'; /* get next digit */
    } while ((n /= 10) > 0);   /* delete it */
    if (sign < 0) {
        s[i++] = '-';
    }
    s[i] = '\0';
    reverse(s);
}


std::array<char, 16> checksum_t::result() {
    hash = XXH3_128bits_digest(&state);
    std::array<char, 16> ret;
    memcpy(&ret, &hash, ret.size());
    return ret;
}


uint32_t checksum_t::hi() {
    hash = XXH3_128bits_digest(&state);
    return hash.high64;
};

uint32_t checksum_t::result32() {
    hash = XXH3_128bits_digest(&state);
    return hash.low64;
};

uint64_t checksum_t::result64() {
    hash = XXH3_128bits_digest(&state);
    return hash.low64;
};

void checksum_init(checksum_t *t) {
    XXH3_128bits_reset(&t->state);
}

void checksum(char *data, size_t len, checksum_t *t) {
    if (XXH3_128bits_update(&t->state, data, len) == XXH_ERROR) {
        rassert(false);
    }
    return;
}



// No error handling other than returning 0, be aware of where you use this function
uint64_t filesize(const STRING& file, bool followlinks = false) {
    // If the user has set followlinks then the directory-traversal, which happens *early*,
    // will resolve links and treat them as files from that point. So a requirement to have
    // knowlege about the flag should not propagate down to here
    rassert(!followlinks);

    try {
        if (fs::is_symlink(file)) {
            if (followlinks) {
                return fs::file_size(fs::read_symlink(file));
            } else {
                return 0;
            }
        }
        return fs::file_size(file);
    } catch (std::exception &) {
        return 0;
    }
}

bool exists(const STRING& file) {
#ifndef _WIN32
    struct stat buf;
    int ret = lstat(file.c_str(), &buf);
    return ret == 0 || (ret != 0 && errno != ENOENT);
#else
    // FIXME test if works for network drives without subdir ('\\localhost\D')
    return PathFileExists(file.c_str());
#endif
}


STRING remove_leading_curdir(const STRING& path) {
    if ((path.length() >= 2 && (path.substr(0, 2) == L(".\\"))) || path.substr(0, 2) == L("./")) {
        return path.substr(2, path.length() - 2);
    } else {
        return path;
    }
}

STRING remove_delimitor(const STRING& path) {
    size_t r = path.find_last_of(L(PATHDELIMS));
    if (r == path.length() - 1) {
        return path.substr(0, r);
    } else {
        return path;
    }
}

STRING remove_leading_delimitor(STRING path) {
    if (path.starts_with(L("\\")) || path.starts_with(L("/"))) {
        path.erase(path.begin());
    }
    return path;
}

bool ISNAMEDPIPE(int attributes) {
#ifdef _WIN32
    (void)attributes;
    return false;
#else
    return (attributes & S_IFIFO) == S_IFIFO;
#endif
}

bool ISDIR(int attributes) {
#ifdef _WIN32
    return ((FILE_ATTRIBUTE_DIRECTORY & attributes) != 0);
#else
    return S_ISDIR(attributes);
#endif
}

bool ISLINK(int attributes) {
#ifdef _WIN32
    return ((FILE_ATTRIBUTE_REPARSE_POINT & attributes) != 0);
#else
    return S_ISLNK(attributes);
#endif
}

bool ISREG(int attributes) {
#ifdef _WIN32
    return !ISDIR(attributes) && !ISNAMEDPIPE(attributes);
#else
    return S_ISREG(attributes);
#endif
}

bool ISSOCK(int attributes) {
#ifdef _WIN32
    return false;
#else
    return S_ISSOCK(attributes);
#endif
}

int get_attributes(STRING path, bool follow) {
#ifdef _WIN32
    if (path.size() > 250) {
        path = std::wstring(L"\\\\?\\") + path;
    }

    (void)follow;
    if (path.length() == 2 && path.substr(1, 1) == L(":")) {
        path = path + L("\\");
    }

    DWORD attributes = GetFileAttributesW(path.c_str());

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        attributes = GetFileAttributesW(STRING(remove_delimitor(path)).c_str());
    }

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        attributes = GetFileAttributesW(STRING(remove_delimitor(path) + DELIM_STR).c_str());
    }

    if (INVALID_FILE_ATTRIBUTES == attributes) {
        return -1;
    }

    return attributes;

#else
    struct stat s;
    if (follow) {
        if (stat(path.c_str(), &s) < 0) {
            return -1;
        }
    } else {
        if (lstat(path.c_str(), &s) < 0) {
            return -1;
        }
    }

    return s.st_mode;

#endif
}

bool set_attributes([[maybe_unused]] const STRING &path, [[maybe_unused]] int attributes) {
    if (attributes == 0) {
        // Data from stdin is assigned 0
        return true;
    }
#ifdef _WIN32
    attributes = attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM);
    BOOL b = SetFileAttributesW(path.c_str(), attributes);
    return b;
#else
    if (chmod(path.c_str(), attributes) == 0) {
        return true;
    }
    mode_t fallback = attributes & 0777; // keep rwx bits only    
    chmod(path.c_str(), fallback);
    return false;
#endif
}

bool is_dir(const STRING& path) { return ISDIR(get_attributes(path, false)); }

void *tmalloc(size_t size) {
    void *p = malloc(size);
    return p;
}

vector<STRING> split_string(STRING str, STRING delim) {
    size_t cutAt;
    vector<STRING> results;
    while ((cutAt = str.find_first_of(delim)) != str.npos) {
        if (cutAt > 0) {
            results.push_back(str.substr(0, cutAt));
        }
        str = str.substr(cutAt + 1);
    }

    if (str.length() > 0) {
        results.push_back(str);
    }
    return results;
}

bool create_directory(const STRING& path) {
#ifdef _WIN32
    return !CreateDirectoryW(path.c_str(), 0);
#else
    return mkdir(path.c_str(), 0777);
#endif
}

bool create_directories(const STRING& path, time_ms_t t) {
    bool b = std::filesystem::create_directories(path);
    if(t != 0) {
        set_date(path, t);
    }
    return b;
}

bool equal2(const void *src1, const void *src2, size_t len) {
    char *s1 = (char *)src1;
    char *s2 = (char *)src2;

    for (unsigned int i = 0; i < len; i++) {
        if (s1[i] != s2[i]) {
            return false;
        }
    }
    return true;
}

bool same2(char *src, size_t len) {
    for (unsigned int i = 0; i < len; i++) {
        if (src[i] != src[0]) {
            return false;
        }
    }
    return true;
}

STRING del(int64_t l, size_t width) {
    CHR s[50], d[50];
    unsigned int i, j = 0;

    memset(s, 0, sizeof(s));
    memset(d, 0, sizeof(d));

    if (l == -1) {
        return STRING(CHR(' '), width);
    }

#ifdef _WIN32
    SPRINTF(s, L("%I64d"), l);
#else
    SPRINTF(s, L("%llu"), static_cast<unsigned long long>(l));
#endif
    for (i = 0; i < STRLEN(s); i++) {
        if ((STRLEN(s) - i) % 3 == 0 && i != 0) {
            d[j] = ',';
            j++;
        }
        d[j] = s[i];
        j++;
    }

    STRING t = STRING(width > STRLEN(d) ? width - STRLEN(d) : 0, CHR(' '));
    t.append(STRING(d));
    return t;
}

size_t longest_common_prefix(const vector<STRING>& strings, bool case_sensitive) {
    if (strings.size() == 0) {
        return 0;
    }

    if (strings.size() == 1) {
        return strings[0].length();
    }

    size_t pos = 0;
    for (;;) {
        STRING c = L(""), d = L("");
        for (unsigned int i = 0; i < strings.size(); i++) {
            if (strings[i].length() < pos + 1) {
                return pos;
            }

            if (i == 0) {
                c = strings[i].substr(pos, 1);
            }

            d = strings[i].substr(pos, 1);

            if (case_sensitive && c.compare(d) != 0) {
                return pos;
            }

            if (!case_sensitive && lcase(c).compare(lcase(d)) != 0) {
                return pos;
            }
        }
        pos++;
    }
}

bool same_path(const STRING& p1, STRING p2) {
    return CASESENSE(abs_path(p1)) == CASESENSE(abs_path(p2));
}

void set_bold(bool bold) {
#ifdef _WIN32
    static WORD original = 7;
    WORD wColor;
    HANDLE hStdOut = GetStdHandle(STD_ERROR_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (bold) {
        if (GetConsoleScreenBufferInfo(hStdOut, &csbi)) {
            original = csbi.wAttributes & 0x0F;
            if (original == 7) {
                wColor = (csbi.wAttributes & 0xF0) + (0xf & 0x0F);
                SetConsoleTextAttribute(hStdOut, wColor);
            }
        }
    } else if (!bold) {
        if (GetConsoleScreenBufferInfo(hStdOut, &csbi)) {
            wColor = (csbi.wAttributes & 0xF0) + (original & 0x0F);
            SetConsoleTextAttribute(hStdOut, wColor);
        }
    }
#else
    if (bold) {
        FPRINTF(stderr, L("\033[1m"));
    } else {
        FPRINTF(stderr, L("\033[0m"));
    }

#endif
}

void tm_to_short(short_tm *s, tm *l) {
    s->tm_year = static_cast<short int>(l->tm_year);
    s->tm_sec = static_cast<unsigned char>(l->tm_sec);
    s->tm_min = static_cast<unsigned char>(l->tm_min);
    s->tm_hour = static_cast<unsigned char>(l->tm_hour);
    s->tm_mday = static_cast<unsigned char>(l->tm_mday);
    s->tm_mon = static_cast<unsigned char>(l->tm_mon);
    s->tm_wday = static_cast<unsigned char>(l->tm_wday);
    s->tm_yday = static_cast<unsigned char>(l->tm_yday);
    s->tm_isdst = static_cast<unsigned char>(l->tm_isdst);
}

void tm_to_long(short_tm *s, tm *l) {
    l->tm_sec = s->tm_sec;
    l->tm_min = s->tm_min;
    l->tm_hour = s->tm_hour;
    l->tm_mday = s->tm_mday;
    l->tm_mon = s->tm_mon;
    l->tm_year = s->tm_year;
    l->tm_wday = s->tm_wday;
    l->tm_yday = s->tm_yday;
    l->tm_isdst = s->tm_isdst;
}

string regx(std::string input, std::string pattern) {
    std::regex regexPattern(pattern);
    std::smatch matchResult;
    if (std::regex_search(input, matchResult, regexPattern)) {
        return matchResult[0].str();
    }
    return {};
}

#ifdef _WIN32
bool is_symlink_consistent(const std::wstring &symlinkPath) {
    // Step 1: Check if it's a reparse point (i.e., symlink or junction)
    DWORD linkAttr = GetFileAttributesW(symlinkPath.c_str());
    if (linkAttr == INVALID_FILE_ATTRIBUTES)
        return false;

    if (!(linkAttr & FILE_ATTRIBUTE_REPARSE_POINT))
        return false; // Not a symlink

    bool linkSaysDirectory = (linkAttr & FILE_ATTRIBUTE_DIRECTORY) != 0;

    // Step 2: Try to open the target normally (follow the link)
    HANDLE h = CreateFileW(symlinkPath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

    if (h == INVALID_HANDLE_VALUE)
        return false; // Could be broken link or access denied

    // Step 3: Check the target type using GetFileInformationByHandle
    BY_HANDLE_FILE_INFORMATION info;
    bool ok = GetFileInformationByHandle(h, &info);
    CloseHandle(h);
    if (!ok)
        return false;

    bool targetIsDirectory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    // Step 4: Compare what the link claims vs what the target really is
    return linkSaysDirectory == targetIsDirectory;
}
#endif