// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include <cfenv>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <time.h>
#include <assert.h>
#include <tuple>
#include "unicode.h"
#include "utilities.hpp"

#include "libexdupe/xxHash/xxh3.h"
#include "libexdupe/xxHash/xxhash.h"

#ifdef WINDOWS
#include "Shlwapi.h"
#else
unsigned int GetTickCount() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#endif

namespace fs = std::filesystem;

#ifdef _WIN32
#pragma warning(disable : 4267)
#pragma warning(disable : 4244)
#endif

#ifdef WINDOWS
#define CURDIR UNITXT(".\\")
#define DELIM_STR UNITXT("\\")
#define DELIM_CHAR '\\'
#else
#define CURDIR UNITXT("./")
#define DELIM_STR UNITXT("/")
#define DELIM_CHAR UNITXT('/')
#endif

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

std::string format_size(uint64_t size) {
    if (size <= 999) {
        return std::to_string(size) + " ";
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
    string ret = oss.str();
    return ret;
}

STRING s2w(const std::string &s) { return STRING(s.begin(), s.end()); }
string w2s(const STRING &s) { return string(s.begin(), s.end()); }

STRING left(const STRING &s) {
    const size_t t = s.find_last_of(UNITXT("/\\"));
    if (t != string::npos) {
        return s.substr(0, t);
    } else {
        return UNITXT("");
    }
}

STRING right(const STRING &s) {
    const size_t t = s.find_last_of(UNITXT("/\\"));
    if (t != string::npos) {
        return s.substr(t + 1);
    } else {
        return UNITXT("");
    }
}

uint64_t rnd64() {
    std::random_device rd;
    std::mt19937_64 eng(rd());
    std::uniform_int_distribution<uint64_t> distr;
    return distr(eng);
}

string wstring2string(STRING wstr) {
    string str(wstr.length(), ' ');
    copy(wstr.begin(), wstr.end(), str.begin());
    return str;
}

STRING string2wstring(string str) {
    STRING wstr(str.length(), L' ');
    copy(str.begin(), str.end(), wstr.begin());
    return wstr;
}

void replace_str(std::STRING &str, const std::STRING &oldStr, const std::STRING &newStr) {
    size_t pos = 0;
    while ((pos = str.find(oldStr, pos)) != std::STRING::npos) {
        str.replace(pos, oldStr.length(), newStr);
        pos += newStr.length();
    }
}

void replace_stdstr(std::string &str, const std::string &oldStr, const std::string &newStr) {
    size_t pos = 0;
    while ((pos = str.find(oldStr, pos)) != std::STRING::npos) {
        str.replace(pos, oldStr.length(), newStr);
        pos += newStr.length();
    }
}

void set_date(STRING file, time_ms_t date) {
    time_t t = std::chrono::system_clock::to_time_t(date);
    tm *timeInfo = gmtime(&t);
#ifdef WINDOWS
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
    HANDLE hFile;
    auto abspath = abs_path(file);
    hFile = CreateFileW(abspath.c_str(), FILE_GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    b = (bool)SetFileTime(hFile, &ft, &ft, &ft);
    CloseHandle(hFile);
#else
    if(fs::is_symlink(file)) {
        struct timespec times[2];
        times[0].tv_sec = times[1].tv_sec = t;
        times[0].tv_nsec = times[1].tv_nsec = 0;
        utimensat(AT_FDCWD, file.c_str(), times, AT_SYMLINK_NOFOLLOW);
    }
    else {
        struct utimbuf Time;
        timeInfo->tm_year -= 1900;
        Time.actime = t;
        Time.modtime = t;
        utime(file.c_str(), &Time);
    }

#endif
}

bool is_symlink(STRING file) { return ISLINK(get_attributes(file, false)); }

bool is_named_pipe(STRING file) { return ISNAMEDPIPE(get_attributes(file, false)); }

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
    targetPath = std::filesystem::read_symlink(symbolicLinkPath);
    return true;
#else
#if 1
    // std does not work on Windows if the symlink points to a non-existant directory
    targetPath = std::filesystem::read_symlink(symbolicLinkPath);
    is_dir = std::filesystem::is_directory(symbolicLinkPath);
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

struct tm local_time_tm(const time_ms_t &t) {
    struct tm localTime;
    time_t t_s = std::chrono::system_clock::to_time_t(t);
#if defined(_MSC_VER) || defined(__MINGW32__)
    localtime_s(&localTime, &t_s);
#else
    struct tm *tmp = localtime(&t_s);
    if (tmp)
        localTime = *tmp;
#endif

    return localTime;
}

#ifdef WINDOWS
std::chrono::time_point<std::chrono::system_clock> filetime_to_timepoint(const FILETIME& ft) {
    std::chrono::file_clock::duration d{ (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime };
    std::chrono::system_clock::time_point tp{ d };
    return tp;
}
#endif

// Returns {created time, modified time} on Windows and {status change time, modified time} on nix
pair<time_ms_t, time_ms_t> get_date(STRING file) {
#ifdef WINDOWS
    FILETIME fileTimeModified;
    FILETIME fileTimeCreated;

    HANDLE hFile = CreateFile(file.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT , NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        if (GetFileTime(hFile, &fileTimeCreated, nullptr, &fileTimeModified)) {
            auto created_tp = filetime_to_timepoint(fileTimeCreated);
            auto modified_tp = filetime_to_timepoint(fileTimeModified);
            return {created_tp, modified_tp};
        } else {
            return {};
        }
        CloseHandle(hFile);
    } else {
        return {};
    }
#else
    struct stat attrib;

    if (is_symlink(file)) {
        lstat(file.c_str(), &attrib);
    } else {
        stat(file.c_str(), &attrib);
    }


    auto duration = std::chrono::seconds(attrib.st_mtim.tv_sec) + std::chrono::nanoseconds(attrib.st_mtim.tv_nsec);
    auto m = std::chrono::time_point<std::chrono::system_clock>(duration);


    return {m, m};
#endif
}

STRING abs_path(STRING source) {
    CHR destination[5000];
    CHR *r;
#ifdef WINDOWS
    r = _wfullpath(destination, source.c_str(), 5000);
    return r == 0 ? UNITXT("") : destination;
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
    return r == 0 ? UNITXT("") : destination;
#endif
}

STRING slashify(STRING path) {
#ifdef WINDOWS
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




std::string checksum_t::result() {
    hash = XXH3_128bits_digest(&state);
    return string((char*)&hash, 16);
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
        abort(false, UNITXT("xxHash error"));
    }
    return;
}



// No error handling other than returning 0, be aware of where you use this function
uint64_t filesize(STRING file, bool followlinks = false) {
    // If the user has set followlinks then the directory-traversal, which happens *early*,
    // will resolve linksand treat them as files from that point. So a requirement to have
    // knowlege about the flag should not propagate down to here
    assert(followlinks == false);

    try {
        if (fs::is_symlink(file)) {
            if (followlinks) {
                return fs::file_size(fs::read_symlink(file));
            } else {
                return 0;
            }
        }
        return fs::file_size(file);
    } catch (exception &) {
        return 0;
    }
}

bool exists(STRING file) {
#ifndef WINDOWS
    struct stat buf;
    int ret = lstat(file.c_str(), &buf);
    return ret == 0 || (ret != 0 && errno != ENOENT);
#else
    // FIXME test if works for network drives without subdir ('\\localhost\D')
    return PathFileExists(file.c_str());
#endif
}


STRING remove_leading_curdir(STRING path) {
    if ((path.length() >= 2 && (path.substr(0, 2) == UNITXT(".\\"))) || path.substr(0, 2) == UNITXT("./")) {
        return path.substr(2, path.length() - 2);
    } else {
        return path;
    }
}

STRING remove_delimitor(STRING path) {
    size_t r = path.find_last_of(UNITXT("/\\"));
    if (r == path.length() - 1) {
        return path.substr(0, r);
    } else {
        return path;
    }
}

STRING remove_leading_delimitor(STRING path) {
    if (path.starts_with(UNITXT("\\")) || path.starts_with(UNITXT("/"))) {
        path.erase(path.begin());
    }
    return path;
}

bool ISNAMEDPIPE(int attributes) {
#ifdef WINDOWS
    (void)attributes;
    return false;
#else
    return (attributes & S_IFIFO) == S_IFIFO;
#endif
}

bool ISDIR(int attributes) {
#ifdef WINDOWS
    return ((FILE_ATTRIBUTE_DIRECTORY & attributes) != 0);
#else
    return S_ISDIR(attributes);
#endif
}

bool ISLINK(int attributes) {
#ifdef WINDOWS
    return ((FILE_ATTRIBUTE_REPARSE_POINT & attributes) != 0);
#else
    return S_ISLNK(attributes);
#endif
}

bool ISREG(int attributes) {
#ifdef WINDOWS
    return !ISDIR(attributes) && !ISNAMEDPIPE(attributes);
#else
    return S_ISREG(attributes);
#endif
}

bool ISSOCK(int attributes) {
#ifdef WINDOWS
    return false;
#else
    return S_ISSOCK(attributes);
#endif
}

int get_attributes(STRING path, bool follow) {
#ifdef WINDOWS
    if (path.size() > 250) {
        path = wstring(L"\\\\?\\") + path;
    }

    (void)follow;
    if (path.length() == 2 && path.substr(1, 1) == UNITXT(":")) {
        path = path + UNITXT("\\");
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

bool set_attributes(STRING path, int attributes) {
#ifdef WINDOWS
    attributes = attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM);
    BOOL b = SetFileAttributesW(path.c_str(), attributes);
    return b;
#else
    return false;
#endif
}

bool is_dir(STRING path) { return ISDIR(get_attributes(path, false)); }

std::string s(uint64_t l) { return std::to_string(l); }

void *tmalloc(size_t size) {
    void *p = malloc(size);

    abort(p == 0, UNITXT("Error at malloc() of %d MB. System out of memory."), (int)(size >> 20));
    return p;
}

#ifdef WINDOWS
int DeleteDirectory(const TCHAR *sPath) {
    HANDLE hFind; // file handle
    WIN32_FIND_DATA FindFileData;

    TCHAR DirPath[MAX_PATH_LEN];
    TCHAR FileName[MAX_PATH_LEN];

    wcscpy(DirPath, sPath);
    wcscat(DirPath, UNITXT("\\"));
    wcscpy(FileName, sPath);
    wcscat(FileName, UNITXT("\\*")); // searching all files

    hFind = FindFirstFile(FileName, &FindFileData); // find the first file
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (FindFileData.cFileName == STRING(UNITXT(".")) || FindFileData.cFileName == STRING(UNITXT(".."))) {
                continue;
            }

            wcscpy(FileName + STRLEN(DirPath), FindFileData.cFileName);

            if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                // we have found a directory, recurse
                if (!DeleteDirectory(FileName)) {
                    break; // directory couldn't be deleted
                }
            } else {
                if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
                    _wchmod(FileName, _S_IWRITE); // change read-only file mode
                }

                if (!DeleteFile(FileName)) {
                    break; // file couldn't be deleted
                }
            }
        } while (FindNextFile(hFind, &FindFileData));

        FindClose(hFind); // closing file handle
    }

    return RemoveDirectoryW(sPath); // remove the empty (maybe not) directory
}
#endif

int delete_directory(STRING base_dir) {
#ifdef WINDOWS
    DeleteDirectory(base_dir.c_str());
#else

    STRING path;
    struct dirent *entry;
    DIR *dir;

    // process files
    if ((dir = opendir(base_dir.c_str())) != 0) {
        while ((entry = readdir(dir))) {
            if (STRING(entry->d_name) != UNITXT(".") && STRING(entry->d_name) != UNITXT("..")) {
                path = base_dir + DELIM_STR + STRING(entry->d_name);
                if (!is_dir(path)) {
                    REMOVE(path.c_str());
                }
            }
        }
        closedir(dir);
    }

    // process directories
    if ((dir = opendir(base_dir.c_str())) != 0) // todo, subst with findfirst because of unc paths like //?/ not
                                                // supported by stat()
    {
        while ((entry = readdir(dir))) {
            path = base_dir + DELIM_STR + STRING(entry->d_name);

            if (is_dir(path) && STRING(entry->d_name) != UNITXT(".") && STRING(entry->d_name) != UNITXT("..")) {
                delete_directory(path);
                rmdir(path.c_str());
            }
        }
        closedir(dir);
    }
    rmdir(base_dir.c_str());
#endif
    return 0;
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

bool create_directory(STRING path) {
#ifdef WINDOWS
    return !CreateDirectoryW(path.c_str(), 0);
#else
    return mkdir(path.c_str(), 0777);
#endif
}

bool create_directories(STRING path, time_ms_t t) {
    bool b = std::filesystem::create_directories(path);
    set_date(path, t);
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

std::STRING del(int64_t l, size_t width = 0) {
    CHR s[50], d[50];
    unsigned int i, j = 0;

    memset(s, 0, sizeof(s));
    memset(d, 0, sizeof(d));

    if (l == -1) {
        return std::STRING(CHR(' '), width);
    }

#ifdef WINDOWS
    SPRINTF(s, UNITXT("%I64d"), l);
#else
    SPRINTF(s, UNITXT("%llu"), static_cast<unsigned long long>(l));
#endif
    for (i = 0; i < STRLEN(s); i++) {
        if ((STRLEN(s) - i) % 3 == 0 && i != 0) {
            d[j] = ',';
            j++;
        }
        d[j] = s[i];
        j++;
    }

    std::STRING t = std::STRING(width > STRLEN(d) ? width - STRLEN(d) : 0, CHR(' '));
    t.append(std::STRING(d));
    return t;
}

size_t longest_common_prefix(vector<STRING> strings, bool case_sensitive) {
    if (strings.size() == 0) {
        return 0;
    }

    if (strings.size() == 1) {
        return strings[0].length();
    }

    size_t pos = 0;
    for (;;) {
        STRING c = UNITXT(""), d = UNITXT("");
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

bool same_path(STRING p1, STRING p2) {
    return CASESENSE(abs_path(p1)) == CASESENSE(abs_path(p2));
}

void set_bold(bool bold) {
#ifdef WINDOWS
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
        FPRINTF(stderr, UNITXT("\033[1m"));
    } else {
        FPRINTF(stderr, UNITXT("\033[0m"));
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
