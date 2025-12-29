// SPDX-License-Identifier: MIT
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2025: Lasse Mikkel Reinhold

#define VER_MAJOR 4
#define VER_MINOR 0
#define VER_REVISION 0
#define VER_DEV 11

#define Q(x) #x
#define QUOTE(x) Q(x)

#if VER_DEV > 0
#define VER QUOTE(VER_MAJOR) "." QUOTE(VER_MINOR) "." QUOTE(VER_REVISION) ".dev" QUOTE(VER_DEV)
#else
#define VER QUOTE(VER_MAJOR) "." QUOTE(VER_MINOR) "." QUOTE(VER_REVISION)
#endif

#define NOMINMAX
#include <algorithm>
#include <assert.h>
#include <chrono>
#include <cmath>
#include <errno.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <regex>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <utility>
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <set>
#include <map>
#include <atomic>
#include <mutex>

#ifdef _WIN32
const bool WIN = true;
#include "shadow/shadow.h"
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <cwctype>

#define CURDIR L(".\\")
#define DELIM_STR L("\\")
#define DELIM_CHAR L('\\')
#define LONGLONG L("%I64d")
#else
#define unsnap(x) x
#define _isatty isatty
#define _fileno fileno

const bool WIN = false;

#include "dirent.h"
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#define CURDIR L("./")
#define DELIM_STR L("/")
#define DELIM_CHAR '/'
#define LONGLONG L("%lld")
#endif

#include "io.hpp"
#include "libexdupe/libexdupe.h"
#include "luawrapper.h"
#include "timestamp.h"
#include "ui.hpp"
#include "unicode.h"
#include "utilities.hpp"
#include "contents_t.h"

#include "file_types.cppm"
#include "identical_files.cppm"
#include "untouched_files.cppm"

#include "abort.h" // hack. Error handling is rewritten in eXdupe 4.x

// Modules reverted. Not well supported yet.

#define ZSTD_STATIC_LINKING_ONLY
#include "libexdupe/zstd/lib/zstd.h"
#include "libexdupe/gxhash/gxhash.h"

#include "xattr_acl.h"

//import FileTypes;
//import IdenticalFiles;
//import UntouchedFiles;

#ifdef _WIN32
#pragma warning(disable : 4459) // todo
#endif

const size_t K = 1024;
const size_t M = 1024 * K;
const size_t G = 1024 * M;

// Keep DISK_READ_CHUNK > DEDUPE_LARGE > DEDUPE_SMALL and all power of 2

// Identical data is being searched (deduped) in two different block sizes, one
// intended for short range compression and the other across terabyte ranges.
// Increase values to improve compression ratio for large data sets; decrease to
// improve for small data sets (also increase memory_usage to improve for any
// size).
size_t DEDUPE_SMALL = 4 * K;
size_t DEDUPE_LARGE = 128 * K;

// Data is read from disk and deduplicated in DISK_READ_CHUNK bytes at a time
// during backup.
const size_t DISK_READ_CHUNK = 1 * M;

// Restore takes part by resolving a tree structure of backwards references in
// past data. Resolve RESTORE_CHUNKSIZE bytes of payload at a time (too large
// value can potentially expand a too huge tree; too small value can be slow)
const size_t RESTORE_CHUNKSIZE = 1 * M;

// Keep the last RESTORE_BUFFER bytes of decompressed chunks, so that we
// don't have to seek on the disk while building above mentioned tree. Todo,
// this was benchmarked in 2010, test if still valid today
const size_t RESTORE_BUFFER = 2 * G;

const size_t IDENTICAL_FILE_SIZE = 1;

#define compile_assert(x) extern int __dummy[(int)x];

compile_assert(sizeof(size_t) == 8);

uint64_t start_time = GetTickCount64();
uint64_t start_time_without_overhead;

using std::string;
using std::wstring;
using std::vector;
using std::pair;
using std::format;

// command line flags
uint64_t memory_usage = 2 * G;
bool continue_flag = false;
bool force_flag = false;
bool no_recursion_flag = false;
bool restore_flag = false;
uint32_t threads = 8;
int flags_exist = 0;
bool compress_flag = false;
bool list_flag = false;
bool named_pipes = false;
bool follow_symlinks = false;
bool shadow_copy = false;
bool absolute_path = false;
bool build_info_flag = false;
bool statistics_flag = false;
bool no_timestamp_flag = false;
bool lua_help_flag = false;
bool e_help_flag = false;
bool usage_flag = false;

uint32_t verbose_level = 1;
uint32_t megabyte_flag = 0;
uint32_t gigabyte_flag = 0;
uint32_t threads_flag = 0;
uint32_t compression_level = 2;
uint32_t set_flag = -1;
bool acl_flag = false; // windows only
bool xattr_flag = false; // *nix only
bool xattr_all_flag = false; // *nix only

// statistics to show to user
uint64_t files = 0;
uint64_t dirs = 0;

uint64_t unchanged = 0; // payload of unchanged files between a full and diff backup
uint64_t identical = 0;
uint64_t identical_files_count = 0;
uint64_t high_entropy_files;
uint64_t unchanged_files = 0;
uint64_t contents_size = 0;
uint32_t hash_seed;
bool use_aesni = true;
bool incremental = false;

STRING full;
STRING directory;
vector<STRING> inputfiles;
STRING name;
vector<STRING> restorelist; // optional list of individual files/dirs to restore
vector<STRING> excludelist;
STRING lua = L("");
vector<STRING> shadows;

vector<STRING> entropy_ext;

std::string xattr_pattern; // *nix only

FILE *ofile = 0;
FILE *ifile = 0;

Cio io = Cio();
Statusbar statusbar;

uint64_t bits;

// various
vector<STRING> argv;
int argc;
STRING flags;
STRING output_file;
void *hashtable;
uint64_t file_id_counter = 0;
uint64_t basepay = 0;

const uint64_t max_payload = 20;

FileTypes file_types;
IdenticalFiles identical_files;
UntouchedFiles untouched_files2;

struct file_offset_t {
    STRING filename;
    uint64_t offset = 0;
    FILE *handle = nullptr;
};

vector<file_offset_t> infiles;

// contains multiple packets
struct chunk_t {
    uint64_t payload = 0;
    size_t compressed_length = 0;
    size_t payload_length = 0; // sum of all user payload in all packets in this chunk
    uint64_t archive_offset;
};

struct packet_t {
    bool is_reference = false;
    uint64_t payload = 0;
    size_t payload_length = 0;
    std::optional<uint64_t> payload_reference;
    const char *literals;

};

vector<contents_t> contents; 
vector<contents_t> contents_added;

vector<chunk_t> chunks;
vector<chunk_t> chunks_added;

struct attr_t {
    int attr = 0;
    std::string xattr; // acl/xattr    
};

struct {
    void add(uint64_t id, const vector<char> &v) {
        return;
        while (size > RESTORE_BUFFER) {
            size -= chunks.at(0).second.size();
            chunks.erase(chunks.begin());
        }
        size += v.size();
        chunks.emplace_back(id, v);
    }

    auto find(uint64_t id) {
        return std::find_if(chunks.begin(), chunks.end(), [&](auto &p) { return p.first == id; });
    }
    std::vector<pair<uint64_t, vector<char>>> chunks;
    uint64_t size = 0;
} chunk_cache;

vector<uint64_t> backup_set; // file_id;
uint64_t original_file_size = 0;

std::vector<std::pair<string, uint64_t>> headers;
std::vector<uint64_t> sets;
std::map<uint64_t, contents_t> content_map;

const string file_footer = "END";
const string backup_set_header = "BCKUPSET";
const string all_contents_header = "CONTENTS";
const string hashtable_header = "HASHTBLE";
const string chunks_header = "CHUNKSCH";
const string payload_header = "PAYLOADP";

std::mutex abort_mutex;
std::atomic<int> aborted = 0;

#ifdef _WIN32
void abort(bool b, retvals ret, const std::wstring &s) {
    std::lock_guard<std::mutex> lg(abort_mutex);
    if (!aborted && b) {
        aborted = static_cast<int>(ret);
        statusbar.use_cerr();
        statusbar.print(0, L("%s"), s.c_str());
        CERR << std::endl << s << std::endl;
        cleanup_and_exit(ret); // todo, kill threads first
    }
}
#endif

void abort(bool b, retvals ret, const std::string &s) {
    std::lock_guard<std::mutex> lg(abort_mutex);
    if (!aborted && b) {
        aborted = static_cast<int>(ret);
        STRING w = STRING(s.begin(), s.end());
        statusbar.use_cerr();
        statusbar.print(0, L("%s"), w.c_str());
        cleanup_and_exit(ret); // todo, kill threads first
    }
}

// todo, legacy
void abort(bool b, const CHR *fmt, ...) {
    std::lock_guard<std::mutex> lg(abort_mutex);
    if (!aborted && b) {
        aborted = 1;
        vector<CHR> buf;
        buf.resize(1 * M);
        va_list argv;
        va_start(argv, fmt);
        VSPRINTF(buf.data(), fmt, argv);
        va_end(argv);
        STRING s(buf.data());
        statusbar.use_cerr();
        statusbar.print(0, L("%s"), s.c_str());
        statusbar.print(0, L("ABORTED"), s.c_str());
        cleanup_and_exit(retvals::err_other); // todo, kill threads first
    }
}

uint64_t backup_set_size() {
    // unchanged and identical are not sent to libexdupe
    return dup_counter_payload() + unchanged + identical;
}

// todo, move
void read_hash(FILE* f, contents_t& c) {
    io.read(c.hash.data(), sizeof(c.hash), f);
    c.first = io.read_ui<decltype(c.first)>(f);
    c.last = io.read_ui<decltype(c.last)>(f);
}

void write_hash(FILE* f, const contents_t& c) {
    io.write(&c.hash, c.hash.size(), f);
    io.write_ui<decltype(c.first)>(c.first, f);
    io.write_ui<decltype(c.last)>(c.last, f);
}


template<class T> void ensure_size(std::vector<T> &v, size_t min_size) {
    if (v.size() < min_size) {
        v.resize(min_size);
    }
}

void move_cursor_up() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi);
    COORD newPosition = {csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y - 1};
    SetConsoleCursorPosition(GetStdHandle(STD_ERROR_HANDLE), newPosition);
#else
    std::cout << "\033[1A";
#endif
}

void update_statusbar_backupv3(STRING file, bool message = false) {
    if (verbose_level == 3) {
        statusbar.update(BACKUP, backup_set_size(), io.write_count, file, false, message);
    }
}

void update_statusbar_backup(const STRING& file, bool message = false) {
    if (verbose_level < 3) {
        statusbar.update(BACKUP, backup_set_size(), io.write_count, file, false, message);
    }
}

void update_statusbar_restore(STRING file) {
    statusbar.update(RESTORE, 0, io.write_count, file);
}

STRING date2str(time_ms_t date) {
    if (date == 0) {
        return L("                ");
    }

    CHR dst[1000];
    tm date2 = local_time_tm(date);
    SPRINTF(dst, L("%04u-%02u-%02u %02u:%02u:%02u"), date2.tm_year + 1900, date2.tm_mon + 1, date2.tm_mday, date2.tm_hour, date2.tm_min, date2.tm_sec);
    return STRING(dst);
}

STRING validchars(STRING path) {
#ifdef _WIN32
    const wchar_t replacement = L'\uFFFD'; // official Unicode replacement char

    // Trim leading spaces
    while (!path.empty() && path.front() == L' ')
        path.erase(path.begin());

    // Trim trailing spaces
    while (!path.empty() && path.back() == L' ')
        path.pop_back();

    for (size_t i = 0; i < path.size(); ++i) {
        wchar_t c = path[i];

        // Illegal Windows path characters (colon always illegal per requirement)
        if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' || c == L'|' || c == L'?' || c == L'*') {
            path[i] = replacement;
        }
        // backslash '\' explicitly allowed
    }

    return path;
#else
    return path;
#endif
}


void read_content_item(FILE *file, contents_t &c) {
    uint8_t type = io.read_ui<uint8_t>(file);
    c.directory = ((type >> 0) & 1) == 1;
    c.symlink = ((type >> 1) & 1) == 1;
    c.windows = ((type >> 2) & 1) == 1;
    c.file_id = io.read_compact<uint64_t>(file);
    c.abs_path = slashify(io.read_utf8_string(file), c.windows);
    c.payload = io.read_compact<uint64_t>(file);
    c.name = slashify(io.read_utf8_string(file), c.windows);
    c.link = slashify(io.read_utf8_string(file), c.windows);
    c.size = io.read_compact<uint64_t>(file);
    c.file_c_time = io.read_compact<uint64_t>(file);
    c.file_modified = io.read_compact<uint64_t>(file);
    c.file_change_time = io.read_compact<uint64_t>(file);
    c.attributes = io.read_ui<uint32_t>(file);
    c.duplicate = io.read_compact<uint64_t>(file);   
    read_hash(file, c);

    size_t xattr_acl_size = io.read_compact<uint64_t>(file);
    c.xattr_acl = io.read_bin_string(xattr_acl_size, file);

#ifdef _WIN32
    if (!c.windows) {
        c.abs_path = validchars(c.abs_path);
        c.name = validchars(c.name);
        c.link = validchars(c.link);
    }
#endif
}

vector<contents_t> read_contents(FILE *f) {
    vector<contents_t> ret;
    contents_t c;
    for (auto &h : headers) {
        if (h.first == all_contents_header) {
            io.seek(f, h.second, SEEK_SET);
            uint64_t n = io.read_ui<uint64_t>(f);
            for (uint64_t i = 0; i < n; i++) {
                read_content_item(f, c);
                ret.push_back(c);
                if (c.file_id >= file_id_counter) {
                    file_id_counter = c.file_id + 1;
                }
            }
        }
    }
    return ret;
}


void write_contents_item(FILE *file, const contents_t &c) {
    uint64_t written = io.write_count;
    uint8_t type = ((c.directory ? 1 : 0) << 0) | ((c.symlink ? 1 : 0) << 1) | ((c.windows ? 1 : 0) << 2);

    io.write_ui<uint8_t>(type, file);
    io.write_compact<uint64_t>(c.file_id, file);
    io.write_utf8_string(c.abs_path, file);
    io.write_compact<uint64_t>(c.payload, file);
    io.write_utf8_string(c.name, file);
    io.write_utf8_string(c.link, file);
    io.write_compact<uint64_t>(c.size, file);
    io.write_compact<uint64_t>(c.file_c_time, file);
    io.write_compact<uint64_t>(c.file_modified, file);
    io.write_compact<uint64_t>(c.file_change_time, file);
    io.write_ui<uint32_t>(c.attributes, file);
    io.write_compact<uint64_t>(c.duplicate, file);
    write_hash(file, c);
    
    io.write_compact(c.xattr_acl.size(), file);
    io.write(c.xattr_acl.data(), c.xattr_acl.size(), file);

    contents_size += io.write_count - written;
}

void read_content_map(FILE* file) {
    contents = read_contents(file);
    for (auto &c : contents) {
        content_map.insert({c.file_id, c});
    }
}


// **************************************************************************************************************
//
// Following handful of functions are rather complicated. They resolve a tree of
// backwards-references of data chunks. Todo: Some of this may be simplified
// alot by using existing tree libraries, maybe in STL.
//
// **************************************************************************************************************

uint64_t pay_count = 0;

// todo, change to STL lower_bound
uint64_t belongs_to(uint64_t offset) {
    rassert(!infiles.empty());
    rassert(infiles.at(0).offset == 0, infiles.at(0).offset);

    if (offset >= infiles.at(infiles.size() - 1).offset) {
        return infiles.size() - 1;
    }

    uint64_t lower = 0;
    uint64_t upper = infiles.size() - 1;

    while (upper - lower > 1) {
        uint64_t middle = lower + (upper - lower) / 2;
        if (offset >= infiles.at(middle).offset) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    return lower;
}


uint64_t read_header(FILE *file, uint64_t *lastgood) {
    string header = io.read_bin_string(8, file);
    abort(!header.starts_with("EXDUPE"), L("File is not an eXdupe archive, or archive is corrupted"));
    char major = io.read_ui<uint8_t>(file);
    char minor = io.read_ui<uint8_t>(file);
    char revision = io.read_ui<uint8_t>(file);
    char dev = io.read_ui<uint8_t>(file);

    DEDUPE_SMALL = io.read_ui<uint64_t>(file);
    DEDUPE_LARGE = io.read_ui<uint64_t>(file);

    abort(major != VER_MAJOR, retvals::err_other, format("This file was created with eXdupe version {}.{}.{}. Please use {}.x.x on it", (int)major, (int)minor, (int)revision, major));
    abort(dev != VER_DEV, retvals::err_other, format("This file was created with eXdupe version {}.{}.{}.dev-{}. Please use the exact same version on it", (int)major, (int)minor, (int)revision, (int)dev));

    hash_seed = io.read_ui<uint32_t>(file);

    uint64_t mem = io.read_ui<uint64_t>(file);
    uint64_t last = io.read_ui<uint64_t>(file);
    if (lastgood) {
        *lastgood = last;
    }
    uint64_t zero = io.read_ui<uint64_t>(file);
    rassert(zero == 0);
    return mem;
}

uint64_t seek_to_header(FILE *file, const string &header) {
    //  archive   HEADER  data  sizeofdata  HEADER  data  sizeofdata
    uint64_t orig = io.tell(file);

    for (auto &h : headers) {
        if (h.first == header) {
            io.seek(file, h.second, SEEK_SET);
            return orig;
        }
    }
    abort(true, L("File is not an eXdupe archive, or archive is corrupted"));
    return orig;
}

uint64_t read_chunks(FILE *file) {
    uint64_t added_payload = 0;
    for (auto &h : headers) {
        if (h.first == chunks_header) {
            io.seek(file, h.second, SEEK_SET);
            uint64_t n = io.read_ui<uint64_t>(file);

            for (uint64_t i = 0; i < n; i++) {
                chunk_t ref;
                ref.archive_offset = io.read_ui<uint64_t>(file);
                ref.payload = io.read_ui<uint64_t>(file);
                ref.payload_length = io.read_ui<uint32_t>(file);
                ref.compressed_length = io.read_ui<uint32_t>(file);
                added_payload += ref.payload_length;
                chunks.push_back(ref);
            }
        }
    }
    return added_payload;
}

size_t write_chunks_added(FILE* file) {
    io.write(chunks_header.c_str(), chunks_header.size(), file);
    uint64_t w = io.write_count;
    io.write_ui<uint64_t>(chunks_added.size(), file);

    for (size_t i = 0; i < chunks_added.size(); i++) {
        io.write_ui<uint64_t>(chunks_added.at(i).archive_offset, file);
        io.write_ui<uint64_t>(chunks_added.at(i).payload, file);
        io.write_ui<uint32_t>(static_cast<uint32_t>(chunks_added.at(i).payload_length), file);
        io.write_ui<uint32_t>(static_cast<uint32_t>(chunks_added.at(i).compressed_length), file);
    }
    io.write_ui<uint32_t>(0, file);
    io.write_ui<uint64_t>(io.write_count - w, file);
    return io.write_count - w;
}


uint64_t find_chunk(uint64_t payload) {
    uint64_t lower = 0;
    uint64_t upper = chunks.size() - 1;

    if (chunks.size() == 0) {
        return std::numeric_limits<uint64_t>::max();
    }

    while (upper != lower) {
        uint64_t middle = lower + (upper - lower) / 2;

        if (chunks.at(middle).payload + chunks.at(middle).payload_length - 1 < payload) {
            lower = middle + 1;
        } else {
            upper = middle;
        }
    }

    if (chunks.at(lower).payload <= payload && chunks.at(lower).payload + chunks.at(lower).payload_length - 1 >= payload) {
        return lower;
    } else {
        return std::numeric_limits<uint64_t>::max();
    }
}

vector<packet_t> parse_packets(const char *src, size_t len, size_t basepy) {
    vector<packet_t> ret;
    size_t pos = 0;
    while (pos < len) {
        packet_t ref;
        uint64_t payload;
        size_t len2;
        const char *literal;
        int r = dup_packet_info(src + pos, &len2, &payload, &literal);
        rassert(r == DUP_REFERENCE || r == DUP_LITERAL);
        ref.payload_length = len2;
        ref.payload = basepy;
        basepy += len2;
        if (r == DUP_REFERENCE) {
            // reference
            ref.is_reference = true;
            ref.payload_reference = payload;
        } else if (r == DUP_LITERAL) {
            // raw data chunk
            ref.literals = literal;
            ref.is_reference = false;
        }
        ret.push_back(ref);
        pos += dup_size_compressed(src + pos);
    }
    return ret;
}

vector<packet_t> get_packets(FILE* f, uint64_t base_payload, std::vector<char>& dst) {
    io.read_vector(dst, DUP_CHUNK_HEADER_LEN, 0, f, true);
    size_t r = dup_chunk_size_compressed(dst.data());
    io.read_vector(dst, r - DUP_CHUNK_HEADER_LEN, DUP_CHUNK_HEADER_LEN, f, true);
    size_t d = dup_chunk_size_decompressed(dst.data());
    ensure_size(dst, d);
    size_t s = dup_decompress_chunk(dst.data(), dst.data());
    abort(s == dup_err_malloc, retvals::err_memory, "Out of memory.");
    dst.resize(s);
    auto packets = parse_packets(dst.data(), s, base_payload);
    return packets;
}


bool resolve(uint64_t payload, size_t size, char *dst, FILE *ifile) {
    size_t bytes_resolved = 0;
    while (bytes_resolved < size) {
        uint64_t rr = find_chunk(payload + bytes_resolved);
        rassert(rr != std::numeric_limits<uint64_t>::max());
        chunk_t chunk = chunks.at(rr);
        vector<packet_t> packets;
        vector<char> chunk_buffer;

        // note lifetime issue: packets point into chunk_buffer or into chunk_cache
        if (auto it = chunk_cache.find(rr); it != chunk_cache.chunks.end()) {
            packets = parse_packets(it->second.data(), it->second.size(), chunk.payload);
        } else {
            io.seek(ifile, chunk.archive_offset, SEEK_SET);
            packets = get_packets(ifile, chunk.payload, chunk_buffer);
            chunk_cache.add(rr, chunk_buffer);
        }

        size_t packet_payload_start = chunk.payload;

        for (auto p : packets) {
            if (packet_payload_start + p.payload_length < payload + bytes_resolved) {
                packet_payload_start += p.payload_length;
                continue;
            }

            size_t missing = size - bytes_resolved;
            size_t prior = payload + bytes_resolved - packet_payload_start;
            size_t get = p.payload_length - prior;
            get = get < missing ? get : missing;

            if (p.is_reference) {
                resolve(p.payload_reference.value() + prior, get, dst + bytes_resolved, ifile);
            } else {
                memcpy(dst + bytes_resolved, p.literals + prior, get);
            }

            bytes_resolved += get;
            packet_payload_start += p.payload_length;

            if (bytes_resolved >= size || packet_payload_start > payload + bytes_resolved) {
                break;
            }
        }
    }
    return false;
}

// clang-format off
void print_file(STRING filename, uint64_t size, time_ms_t file_modified = 0) {
#ifdef _WIN32
    statusbar.print_no_lf(0, L("%s  %s  %s\n"), 
        size == std::numeric_limits<uint64_t>::max() ? L("                   ") : del(size, 19).c_str(),
/*
        attributes & FILE_ATTRIBUTE_ARCHIVE ? 'A' : ' ', 
        attributes & FILE_ATTRIBUTE_SYSTEM ? 'S' : ' ',
        attributes & FILE_ATTRIBUTE_HIDDEN ? 'H' : ' ',
        attributes & FILE_ATTRIBUTE_READONLY ? 'R' : ' ',
        attributes & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED ? 'I' : ' ',
*/
        date2str(file_modified).c_str(), filename.c_str());
#else
    statusbar.print_no_lf(0, L("%s  %s  %s\n"), size == std::numeric_limits<uint64_t>::max() ? L("                   ") : del(size, 19).c_str(), date2str(file_modified).c_str(), filename.c_str());
#endif
}
// clang-format on

bool save_directory(STRING base_dir, STRING path, bool write, attr_t a) {
    static STRING last_full = L("");
    static bool first_time = true;

    STRING full = base_dir + path;
    full = remove_delimitor(full) + DELIM_STR;
    STRING full_orig = full;

#ifdef _WIN32
    full = unsnap(full);

    size_t shadowsize = snappart(base_dir + path).size();
    if (shadowsize > 0) {
        if (shadowsize > base_dir.size()) {
            size_t remove = shadowsize - base_dir.size();
            path = path.substr(remove);
            if (path.substr(0, 1) == DELIM_STR) {
                path = path.substr(1);
            }

            path = volpart(full_orig) + path;
        }
    }
#endif
    if (full != last_full || first_time) {
        contents_t c;
        c.attributes = a.attr;
        c.xattr_acl = a.xattr;

        c.directory = true;
        c.symlink = false;

        if (absolute_path) {
            c.name = full;
        } else {
            c.name = path;
        }

        c.link = L("");
        c.payload = 0;
        auto d = get_date(full);
        c.file_c_time = d.created;
        c.file_modified = d.written;
        c.file_change_time = d.changed;
        c.file_id = file_id_counter++;

        contents.push_back(c);
        contents_added.push_back(c);

        backup_set.push_back(c.file_id);

        if (write && !incremental) {
            io.write("I", 1, ofile);
            write_contents_item(ofile, c);
        }

        last_full = full;
        first_time = false;

        return true;
    }
    return false;
}


uint64_t checksum64(const void *src, size_t len, uint32_t hash_seed, int use_aesni) {
    char h[sizeof(uint64_t)];
    gxhash((const uint8_t *)src, len, h, sizeof(uint64_t), hash_seed, use_aesni);
    return *(uint64_t*)h;
}

size_t write_hashtable(FILE *file) {

    size_t t = dup_compress_hashtable(memory_begin);
    io.write(hashtable_header.c_str(), hashtable_header.size(), file);
    io.write_ui<uint64_t>(t, file);
    io.write(memory_begin, t, file);
    auto crc = checksum64(memory_begin, t, hash_seed, use_aesni);
    io.write_ui<uint64_t>(crc, file);
    t += 8;
    io.write_ui<uint64_t>(t + 8, file);
    return t;
}

uint64_t read_hashtable(FILE *file) {
    uint64_t orig = seek_to_header(file, hashtable_header);
    uint64_t s = io.read_ui<uint64_t>(file);
    io.read(memory_end - s, s, file);
    uint64_t crc = io.read_ui<uint64_t>(file);
    uint64_t crc2 = checksum64(memory_end - s, s, hash_seed, use_aesni);
    abort(crc != crc2, L("'%s' is corrupted or not an archive (hashtable checksum)"), full.c_str());
    io.seek(file, orig, SEEK_SET);
    int i = dup_decompress_hashtable(memory_end - s);
    abort(i != 0, L("'%s' is corrupted or not an archive (hashtable structure)"), full.c_str());
    return 0;
}



size_t write_contents_added(FILE *file) {
    io.write(all_contents_header.c_str(), all_contents_header.size(), file);
    uint64_t w = io.write_count;
    io.write_ui<uint64_t>(contents_added.size(), file);
    for (size_t i = 0; i < contents_added.size(); i++) {
        write_contents_item(file, contents_added.at(i));
    }
    io.write_ui<uint32_t>(0, file);
    io.write_ui<uint64_t>(io.write_count - w, file);
    return io.write_count - w;
}



void read_backup_set(FILE *f, uint64_t filepos, time_ms_t &date, uint64_t &size, uint64_t &files, vector<uint64_t>* ret, vector<STRING>* cmd) {
    uint64_t id;
    uint64_t orig = io.seek(f, filepos, SEEK_SET);
    uint64_t n = io.read_ui<uint64_t>(f);
    if (ret) {
        for (uint64_t i = 0; i < n; i++) {
            id = io.read_ui<uint64_t>(f);
            ret->push_back(id);
        }
    } 
    else {
        io.seek(f, n * sizeof(uint64_t), SEEK_CUR);
    }
    date = io.read_ui<uint64_t>(f);
    size = io.read_ui<uint64_t>(f);
    files = io.read_ui<uint64_t>(f);

    if (cmd) {
        uint64_t cmdn = io.read_ui<uint64_t>(f);
        for (uint64_t i = 0; i < cmdn; i++) {
            cmd->push_back(io.read_utf8_string(f));
        }
    }

    io.seek(f, orig, SEEK_SET);
}

size_t write_backup_set(FILE *file, time_ms_t date, uint64_t size, uint64_t files, const std::vector<STRING>& cmd) {
    io.write(backup_set_header.c_str(), backup_set_header.size(), file);
    uint64_t w = io.write_count;
    io.write_ui<uint64_t>(backup_set.size(), file);
    for (size_t i = 0; i < backup_set.size(); i++) {
        io.write_ui<uint64_t>(backup_set.at(i), file);
    }

    io.write_ui<uint64_t>(date, file);
    io.write_ui<uint64_t>(size, file);
    io.write_ui<uint64_t>(files, file);

    io.write_ui<uint64_t>(cmd.size(), file);
    for (auto& c : cmd) {
        io.write_utf8_string(c, file);
    }
    
    io.write_ui<uint64_t>(io.write_count - w, file);
    return io.write_count - w;
}


bool read_headers(FILE* file) {
    bool file_ok = true;
    uint64_t lastgood = 0;
    io.seek(file, 0, SEEK_SET);
    read_header(file, &lastgood);
    io.seek(file, -static_cast<int64_t>(file_footer.size()), SEEK_END); // file was written to stdout that cannot seek to update header

    string e = io.read_bin_string(3, file);
    if (e != file_footer) {
        file_ok = false;
        io.seek(file, lastgood, SEEK_SET);
    } else {
        io.seek(file, -static_cast<int64_t>(file_footer.size()), SEEK_END); // file was written to stdout that cannot seek to update header    
    }

    uint64_t s;
    string h = "";

    for (;;) {
        auto msg = L("Archive is corrupted");
        abort(io.seek(file, -8, SEEK_CUR) != 0, msg);
        s = io.read_ui<uint64_t>(file);
        // file-header ends with a 0
        if (s == 0) {
            return file_ok;
        }
        abort(io.seek(file, -8 - s - 8, SEEK_CUR) != 0, msg);

        h = io.read_bin_string(8, file);

        auto p = make_pair(h, io.tell(file));
        headers.insert(headers.begin(), p);
        if (h == backup_set_header) {
            sets.insert(sets.begin(), io.tell(file));
        }
        abort(io.seek(file, -8, SEEK_CUR) != 0, msg);
    }
    return file_ok;
}


void init_content_maps(FILE* ffull) {
    auto con = read_contents(ffull);
    for(auto& c : con) {
        // fixme, verify this works for .is_dublicate
        if(!c.directory && !c.symlink) {
            untouched_files2.add_during_restore(c);
        }
    }
}

FILE *try_open(STRING file2, char mode, bool abortfail) {
    auto file = file2;
#ifdef _WIN32
    // todo fix properly. A long *relative* path wont work
    if (file.size() > 250) {
        file = wstring(L("\\\\?\\")) + file;
    }
#endif
    FILE *f;
    rassert(mode == 'r' || mode == 'w' || mode == 'a');
    if (file == L("-stdin")) {
        f = stdin;
    } else if (file == L("-stdout")) {
        f = stdout;
    } else {
        f = io.open(file.c_str(), mode);
        abort(!f && abortfail && mode == 'w', L("Error creating file: %s"), file2.c_str());
        abort(!f && abortfail && mode == 'r', L("Error opening file for reading: %s"), file2.c_str());
        abort(!f && abortfail && mode == 'a', L("Error opening file for append: %s"), file2.c_str());
    }

    return f;
}


void write_header(FILE *file, uint64_t mem, uint32_t hash_seed, uint64_t lastgood) {

    io.write("EXDUPE D", 8, file);

    io.write_ui<uint8_t>(VER_MAJOR, file);
    io.write_ui<uint8_t>(VER_MINOR, file);
    io.write_ui<uint8_t>(VER_REVISION, file);
    io.write_ui<uint8_t>(VER_DEV, file);

    io.write_ui<uint64_t>(DEDUPE_SMALL, file);
    io.write_ui<uint64_t>(DEDUPE_LARGE, file);
    io.write_ui<uint32_t>(hash_seed, file);

    io.write_ui<uint64_t>(mem, file);
    io.write_ui<uint64_t>(lastgood, file);

    io.write_ui<uint64_t>(0, file);
}



contents_t get_contents_from_id2(vector<contents_t>& cont, uint64_t id) {
    for (auto &c : cont) {
        if (c.file_id == id) {
            return c;
        }
    }
    abort(true, L("No such id"));
    return {};
}

void list_contents() {
    time_ms_t d = 0;
    uint64_t s = 0;
    uint64_t f = 0;

    FILE *ffile = try_open(full.c_str(), 'r', true);
    uint64_t mem = read_header(ffile, 0);
    read_headers(ffile);

    auto print_item = [](contents_t& c) {
        if (c.symlink) {
            print_file(STRING(c.name + L(" -> ") + STRING(c.link)).c_str(), std::numeric_limits<uint64_t>::max(), c.file_modified);
            files++;
        } else if (c.directory) {
            if (c.name != L(".\\") && c.name != L("./") && c.name != L("")) {
                static STRING last_full = L("");
                static bool first_time = true;

                STRING full = c.name;
                full = remove_delimitor(full);
                STRING full_orig = full;
                // fixme, prevent identical directory names in the archive
                if (full != last_full || first_time) {
                    statusbar.print_no_lf(0, L("%s%s\n"), STRING(full_orig != full ? L("*") : L("")).c_str(), full.c_str());
                    last_full = full;
                    first_time = false;
                }
            }
        } else {
            print_file(c.name, c.size, c.file_modified);
        }
    };

    if (set_flag == static_cast<uint32_t>(-1)) {
        uint64_t prev_c = 0;
        uint64_t total_uncompressed = 0;
        uint64_t total_files = 0;

        auto mbround = [](uint64_t s) {
            return static_cast<uint64_t>(s == 0 ? 0 : s < 512 * 1024 ? 1 : std::round(s / 1024. / 1024.)); };

        statusbar.print(0, L("  Set              Date         Files          Size    Compressed  Command line sources"));
        statusbar.print(0, L("---------------------------------------------------------------------------------------"));
        //                    99999  dddd-dd-dd dd:dd 9,999,999,999 99,999,999 MB       xxxxxxx  xxxxxxxxxxxxxxxxxxxxxxxxxxxx
        for (size_t set = 0; set < sets.size(); set++) {
            uint64_t c = sets.at(set) - prev_c;
            prev_c = sets.at(set);
            vector<STRING> cmd;
            read_backup_set(ffile, sets.at(set), d, s, f, nullptr, &cmd);
            STRING cmdline;
            for (auto &c : cmd) {
                cmdline += L("") + c + L("; ");
            }
            if (cmdline.size() > 2) {
                cmdline = cmdline.substr(0, cmdline.size() - 2);
            }
            if (cmdline.size() > 80) {
                cmdline = cmdline.substr(0, cmdline.size() - 3) + L("..."); }
                
            auto ds = date2str(d);
            ds = ds.substr(0, ds.size() - 3);
            total_uncompressed += s;
            total_files += f;
            s = mbround(s);
            c = mbround(c);
            statusbar.print(0, L("%s  %s %s %s MB  %s MB  %s"), del(set, 5).c_str(), ds.c_str(), del(f, 13).c_str(), del(s, 10).c_str(), del(c, 9).c_str(), cmdline.c_str());
        }
        statusbar.print(0, L("---------------------------------------------------------------------------------------"));
        size_t total_compressed = filesize(full.c_str(), false);
        total_compressed = mbround(total_compressed);
        total_uncompressed = mbround(total_uncompressed);
        statusbar.print(0, L("  Total                 %s %s MB  %s MB"), del(total_files, 13).c_str(), del(total_uncompressed, 10).c_str(), del(total_compressed, 9).c_str());
        statusbar.print(0, L("\nUsing %sB memory during backups, suitable for backup sets of %sB each (set with\n-g flag on initial backup)."), s2w(suffix(mem)).c_str(), s2w(suffix(max_payload * mem)).c_str());
#if 0
        statusbar.print(0, L("\nA few files:"));
        read_content_map(ffile);
        size_t add = content_map.size() / 5 + 1;
        for (size_t i = 0; i < content_map.size(); i += add) {
            auto s = content_map[i].abs_path;
            if (s.empty()) {
                i = i - add + 1;
                continue;
            }
            if (s.length() + 2 > gsl::narrow<size_t>(statusbar.m_term_width)) {
                s = s.substr(0, minimum(s.length(), statusbar.m_term_width - 2 - 2)) + L("..");
            }
            statusbar.print(0, L("  %s"), s.c_str());
        }
#endif 
    } else {
        abort(set_flag >= sets.size(), L("Backup set does not exist")); // fixme, allows you to specify the last set even if its corrupted?
        vector<uint64_t> set;
        read_backup_set(ffile, sets.at(set_flag), d, s, f, &set, nullptr);

        read_content_map(ffile);

        for (auto &id : set) {
            contents_t c = content_map[id];
            print_item(c);
        }
        fclose(ffile);
    }
}

void print_build_info() {
    // "2024-01-04T09:27:05+0100"
    STRING td = L(_TIMEZ_);
    td = td.substr(0, 10) + L(" ") + td.substr(11, 8) + L(" ") + td.substr(19, 5);
    STRING b = STRING(L("ver " VER ", built ")) + td + L(", sha ") + L(GIT_COMMIT_HASH) + L(", ");
#ifdef NDEBUG
    b += L("release mode");
#else
    b += L("debug mode");
#endif
    b += STRING(L(", avx2: ")) + STRING(dup_is_avx2_supported() ? L("yes") : L("no"));
    b += STRING(L(", aes-ni: ")) + STRING(dup_is_aesni_supported() ? L("yes") : L("no"));

    statusbar.print(0, b.c_str());
}

#ifdef _WIN32
vector<STRING> wildcard_expand(vector<STRING> files) {
    HANDLE hFind = INVALID_HANDLE_VALUE;
    DWORD dwError;
    WIN32_FIND_DATAW FindFileData;
    vector<STRING> ret;

    for (uint32_t i = 0; i < files.size(); i++) {
        STRING payload_queue = remove_delimitor(files.at(i));
        STRING s = right(payload_queue) == L("") ? payload_queue : right(payload_queue);
        if (s.find_first_of(L("*?")) == string::npos) {
            ret.push_back(files.at(i));
        } else {
            vector<STRING> f;
            hFind = FindFirstFileW(files.at(i).c_str(), &FindFileData);

            abort(!continue_flag && hFind == INVALID_HANDLE_VALUE, L("Source file(s) '%s' not found"), files.at(i).c_str());

            if (hFind != INVALID_HANDLE_VALUE) {
                if (STRING(FindFileData.cFileName) != L(".") && STRING(FindFileData.cFileName) != L("..")) {
                    f.push_back(FindFileData.cFileName);
                }
                while (FindNextFileW(hFind, &FindFileData) != 0) {
                    if (STRING(FindFileData.cFileName) != L(".") && STRING(FindFileData.cFileName) != L("..")) {
                        f.push_back(FindFileData.cFileName);
                    }
                }

                dwError = GetLastError();
                FindClose(hFind);
                abort(dwError != ERROR_NO_MORE_FILES, L("FindNextFile error. Error is %u"), dwError);

                size_t t = files.at(i).find_last_of(L("\\/"));
                STRING dir = L("");
                if (t != string::npos) {
                    dir = files.at(i).substr(0, t + 1);
                }

                for (uint32_t j = 0; j < f.size(); j++) {
                    ret.push_back(dir + f.at(j));
                }
            }
        }
    }
    return ret;
}
#endif

void tidy_args(int argc2, CHR *argv2[]) {
    int i = 0;
    argc = argc2;

    for (i = 0; i < argc; i++) {
        argv.push_back(argv2[i]);
    }
}

void parse_flags(void) {
    if (argc == 2 && argv.at(1) == L("-u?")) {
        lua_help_flag = true;
        return;
    }
    if (argc == 2 && argv.at(1) == L("-e?")) {
        e_help_flag = true;
        return;
    }

    if (argc == 2 && argv.at(1) == L("-?")) {
        usage_flag = true;
        return;
    }

    int i = 1;

    while (argc > i && argv.at(i).substr(0, 1) == L("-") && argv.at(i).substr(0, 2) != L("--") && argv.at(i) != L("-stdin") && argv.at(i) != L("-stdout")) {
        flags = argv.at(i);
        i++;
        flags_exist++;

        if (flags.length() > 2 && flags.substr(0, 2) == L("-u")) {
            lua = flags.substr(2);
            abort(lua == L(""), L("Missing command in -u flag"));
        } 
        else if (flags.length() > 2 && flags.substr(0, 2) == L("-e")) {
                STRING e = flags.substr(2);
                abort(e == L(""), L("Missing extensions in -e flag"));
                entropy_ext.push_back(e);
        } else if (flags.length() > 2 && flags.substr(0, 2) == L("-s")) {
#ifdef _WIN32
            STRING mount = flags.substr(2);
            abort(mount == L(""), L("Missing drive in -s flag"));
            shadows.push_back(mount);
#else
            abort(true, L("-s flag not supported on *nix"));
#endif
        } else {
            size_t e = flags.find_first_not_of(L("-XACwfhuPRrxqcpiLzksatgmv0123456789B"));
            if (e != string::npos) {
                abort(true, L("Unknown flag -%s"), flags.substr(e, 1).c_str());
            }

            string flagsS = w2s(flags);

            // abort if numeric digits are used with a wrong flag
            if (regx(flagsS, "[^mgwtvsiLxR0123456789][0-9]+") != "") {
                abort(true, L("Numeric values must be preceded by R, m, g, t, v, or x"));
            }

            for (auto t : std::vector<pair<bool &, std::string>>{
                     {no_timestamp_flag, "w"},
                     {no_recursion_flag, "r"},
                     {force_flag, "f"},
                     {continue_flag, "c"},
                     {named_pipes, "p"},
                     {follow_symlinks, "h"},
                     {absolute_path, "a"},
                     {build_info_flag, "B"},
                     {statistics_flag, "k"},
                     {xattr_flag, "X"},
                     {acl_flag, "C"},
                     {xattr_all_flag, "A"},
                 }) {
                if (regx(flagsS, t.second) != "") {
                    t.first = true;
                }
            }

            auto set_int_flag = [&](uint32_t &flag_ref, const string letter, bool required = true) {
                if (regx(flagsS, letter) == "") {
                    return false;
                }
                string f = regx(flagsS, letter + "\\d+");
                abort(required && f == "", L("-%s flag must be an integer"), letter.c_str());
                int i = f == "" ? -1 : atoi(f.substr(1).c_str());
                flag_ref = i;
                return true;
            };

            if (set_int_flag(threads_flag, "t")) {
                if (threads_flag >= 1) {
                    threads = threads_flag;
                } else {
                    abort(true, L("Invalid -t flag value"));
                }
            }

            if (set_int_flag(gigabyte_flag, "g")) {
                if (gigabyte_flag > 0) {
                    memory_usage = gigabyte_flag * G;
                } else {
                    abort(true, L("Invalid -g flag value"));
                }
            }

            if (set_int_flag(megabyte_flag, "m")) {
                if (megabyte_flag > 0) {
                    memory_usage = megabyte_flag * M;
                } else {
                    abort(true, L("Invalid -m flag value"));
                }
            }

            if (set_int_flag(verbose_level, "v")) {
                abort(verbose_level > 3, L("-v flag value must be 0...3"));
            }

            if (set_int_flag(compression_level, "x")) {
                abort(compression_level > 4, L("-x flag value must be 0...4"));
            }

            if (set_int_flag(set_flag, "R")) {
                restore_flag = true;
            }

            if (set_int_flag(set_flag, "L", false)) {
                list_flag = true;
            }

        }
    } // end of while

    if (i == 1 || (!restore_flag && !list_flag)) {
        flags = L("");
        compress_flag = true;
    }

    // todo, add s and p verification
    abort(xattr_flag && xattr_all_flag, L("-X flag not compatible with -A"));
    abort(megabyte_flag != 0 && gigabyte_flag != 0, L("-m flag not compatible with -g"));
    abort(restore_flag && (no_recursion_flag), L("-R flag not compatible with -n or -c"));
    abort(restore_flag && (megabyte_flag != 0 || gigabyte_flag != 0), L("-m and -t flags not applicable to restore (no memory required)"));
    abort(restore_flag && (threads_flag != 0), L("-t flag not supported for restore"));

    if (xattr_flag) {
        xattr_pattern = "^user\\.";
    } else if (xattr_all_flag) {
        xattr_pattern = ".*";
    }

}

void add_item(const STRING &item) {
    if (item.size() >= 2 && item.substr(0, 2) == L("--")) {
        STRING e = item.substr(2);
        e = remove_delimitor(CASESENSE(abs_path(e)));
        if (!(exists(e))) {
            statusbar.print(2, L("Excluded item '%s' does not exist"), e.c_str());
        } else {
            excludelist.push_back(e);
        }
    } else {
        inputfiles.push_back(item);
    }
}

void parse_files(void) {
    if (compress_flag) {
        for (int i = flags_exist + 1; i < argc - 1; i++) {
            add_item(argv.at(i));
        }

        abort(argc - 1 < flags_exist + 2, L("Missing arguments. "));
        full = argv.at(argc - 1);
        if (inputfiles.at(0) == L("-stdin")) {
            abort(argc - 1 < flags_exist + 2, L("Missing arguments. "));
            name = argv.at(flags_exist + 2);
        }

        abort(inputfiles.at(0) == L("-stdout") || name == L("-stdin") || full == L("-stdin") || (inputfiles.at(0) == L("-stdin") && argc < 3 + flags_exist) || (inputfiles.at(0) != L("-stdin") && argc < 3 + flags_exist),
              L("Syntax error in source or destination. "));
    } else if (!compress_flag && !list_flag) {
        abort(argc - 1 < flags_exist + 2, L("Missing arguments. "));
        full = argv.at(1 + flags_exist);
        directory = argv.at(2 + flags_exist);

        abort(full == L("-stdin") && argc - 1 > flags_exist + 2, L("Too many arguments. "));

        for (int i = 0; i < argc - 3 - flags_exist; i++) {
            restorelist.push_back(argv.at(i + 3 + flags_exist));
        }

        abort(directory == L("-stdout") && full == L("-stdin"), L("Restore with both -stdin and -stdout is not supported. One must be a seekable device. "));
        abort(full == L("-stdout") || directory == L("-stdin") || argc < 3 + flags_exist, L("Syntax error in source or destination. "));
    } else if (list_flag) {
        abort(argv.size() < 3, L("Specify a backup file. "));
        abort(argv.size() > 3, L("Too many arguments. "));
        full = argv.at(1 + flags_exist);
    }

    if (compress_flag && inputfiles.at(0) != STRING(L("-stdin"))) {
        vector<STRING> inputfiles2;

        for (uint32_t i = 0; i < inputfiles.size(); i++) {
            STRING f = inputfiles.at(i);
#ifdef _WIN32
            if (f.size() == 2 && f[1] == L':') {
                f += L"\\";
            }
#endif
            if (abs_path(f) == STRING(L(""))) {
                abort(!continue_flag, L("Aborted, does not exist: %s"), f.c_str());
                statusbar.print(2, L("Skipped, does not exist: %s"), f.c_str());
            } else {
                inputfiles2.push_back(abs_path(f));
#ifdef _WIN32
                inputfiles2.back() = snap(inputfiles2.back());
#endif
            }
        }

        inputfiles = inputfiles2;
#ifdef _WIN32
        inputfiles = wildcard_expand(inputfiles);
#endif
    }
}

STRING tostring(std::string s) { return STRING(s.begin(), s.end()); }

void print_usage(bool show_long) {
    std::string long_help = R"(eXdupe %v file archiver. MIT license. Copyright 2010 - 2025

Create first backup:
  exdupe [flags] <sources | -stdin> <backup file | -stdout>

Add incremental backup:
  exdupe [flags] <sources | -stdin> <backup file>

Show available backup sets:
  exdupe -L <backup file>

Show contents of backup set:
  exdupe -L# <backup file>

Restore backup set:
  exdupe -R# [flags] <backup file | -stdin> <dest dir> [items]
  exdupe -R# [flags] <backup file> <dest dir | -stdout> [items]

Show build info: -B

<sources> is a list of files or paths to backup. [items] is a list of files or
paths to restore, written as printed by the -L flag.

Flags:
    -f Overwrite existing files
    -c Continue if a file cannot be read during backup or if ACLs or extended
       attributes cannot be set during restore (default is to abort)
    -w Read contents of files during incremental backup to determine if they
       have changed (default is to look at timestamps only).
   -t# Use # threads (default = 8)
   -g# Use # GB memory for deduplication (default = 2). Set to 1 GB per )" + std::to_string(max_payload) + R"( GB 
       of data in one backup set for best result. Use -m# to specify MB
       instead. Incremental backups will use the same memory as the first
       backup
   -x# Use compression level # after deduplication (0, 1, 2 = default, 3, 4).
       Level 0 means no compression and lets you apply your own
    -- Prefix items in the <sources> list with "--" to exclude them
    -p Include named pipes
    -h Follow symlinks (default is to store symlink only)
    -a Store absolute and complete paths (default is to identify and remove
       any common parent path of the items passed on the command line).
    -X Get or set xattr in user namespace (Linux only)
    -A Get or set all xattr in all namespaces (Linux only)
    -C Get or set ACLs (Windows only)
-s"x:" Use Volume Shadow Copy Service for local drive x: (Windows only)
 -u"s" Filter away files or directories with a Lua script. See more with -u?
  -v#  Verbosity # (0 = quiet, 1 = status bar, 2 = skipped files, 3 = all)
   -k  Show deduplication statistics at the end
 -e"x" Don't apply compression or deduplication to files with the file extension
       x. See more with -e?

Example of backup, incremental backups and restore:
  exdupe my_dir backup.exd
  exdupe my_dir backup.exd
  exdupe my_dir backup.exd
  exdupe -R1 backup.exd restore_dir

More examples:
  exdupe -t12 -g8 dir1 dir2 backup.exd
  exdupe -R0 backup.exd restore_dir dir2%/file.txt
  exdupe file.txt -stdout | exdupe -R0 -stdin restore_dir)";

    std::string short_help = R"(Create first backup:
  exdupe [flags] <sources | -stdin> <backup file | -stdout>

Add incremental backup:
  exdupe [flags] <sources | -stdin> <backup file>

Show available backup sets:
  exdupe -L <backup file>

Restore backup set:
  exdupe -R# [flags] <backup file | -stdin> <dest dir>
  exdupe -R# [flags] <backup file> <dest dir | -stdout>

A few flags:
  -f Overwrite existing files
  -c Continue if a file cannot be read during backup or if ACLs or extended
     attributes cannot be set during restore (default is to abort)
 -g# Use # GB memory for deduplication (default = 2). Set to 1 GB per )" +
                             std::to_string(max_payload) + R"( GB of
     data in one backup set for best result
 -x# Use compression level # after deduplication (0, 1, 2 = default, 3, 4)
  -? Show complete help)";
 
    auto delim = [](std::string& s) {
        s = std::regex_replace(s, std::regex("%/"), WIN ? "\\" : "/");
        s = std::regex_replace(s, std::regex("%v"), VER);
    };
    delim(short_help);
    delim(long_help);

    statusbar.print(0, show_long ? tostring(long_help).c_str() : tostring(short_help).c_str());

    if (VER_DEV != 0) {
        statusbar.print(0, L("\nHIGHLY UNSTABLE PREVIEW VERSION"));
    }

    if (!use_aesni) {
        statusbar.print(0, L("\nNOTE: AES-NI CPU feature not detected - performance will be very slow. Check\nyour setup if running in a virtual machine."));
    }
}

void print_e_help() {
    STRING ext;
    for(size_t i = 0; i < file_types.types.size(); i++) {
        auto e = file_types.types[i].extension;
        e = e.substr(1, e.size() - 1);
        ext += e;
        if(i + 1 < file_types.types.size()) {
            ext += L(", ");
        }
    }

    std::string e_help = R"del(Default files stored without compression or deduplication are:

)del" + w2s(ext) + "." + R"del(

Compressed archives like zip and gz are not excluded by default because some
may benefit from deduplication.

You can use multiple -e flags such as -e"rar" -e"flac".)del";

    statusbar.print(0, tostring(e_help).c_str());
}

void print_lua_help() {
    std::string lua_help = R"del(You can provide a Lua script that gets called for each item during backup:
  exdupe -u"return true" . backup.exd

If the script returns true the item will be added, else it will be skipped.

You can reference following variables:
  path:   Absolute path
  is_*:   Boolean variables is_dir, is_file, is_link
  name:   Name without path
  ext:    Extension or empty if no period exists
  size:   Size in bytes
  attrib: Result of chmod on Linux. On Windows you can reference the booleans
          FILE_ATTRIBUTE_READONLY, FILE_ATTRIBUTE_HIDDEN, etc.
  time:   Last modified time as os.date object. You can also reference these
          integer variables: year, month, day, hour, min, sec

Extra helper functions:
  contains({list}, value): Test if the list contains the value

All Lua string functions work in utf-8. If path, name or ext are not valid
utf-8 it will be converted by replacing all bytes outside basic ASCII (a-z, A-Z,
0-9 and common symbols) by '?' before being passed to your script.

String and path comparing is case sensitive, but string.upper() and string.
lower() will only change basic ASCII letters. Any other letters remain
unchanged.

Remember to return true for directories to traverse them.

Simple examples:
  -v0 -u"print('added ' .. path .. ': ' .. size); return true"
  -u"return year >= 2024 or is_dir"
  -u"return size < 1000000 or is_dir"
  -u"return not contains({'tmp', 'temp'}, lower(ext))"
  -u"return (is_dir and not (name == '.git')) or (not is_dir)"

Example of skipping directories that begin with http+++ or https+++:
  -u"return (is_dir and name:find('^https?%%+%%+%%+') == nil) or (not is_dir)")del";
    // todo, get rid of print(const CHR *fmt)
    statusbar.print(0, tostring(lua_help).c_str());
}



STRING parent_path(const vector<STRING> &items) {
    size_t prefix = longest_common_prefix(items, !WIN);
    if (prefix == 0) {
        return L("");
    }

    for (uint32_t i = 0; i < items.size(); i++) {
        if (items.at(i).size() == prefix || items.at(i).substr(prefix - 1, 1) == STRING(DELIM_STR)) {
            // Do nothing
        } else {
            return left(items.at(0).substr(prefix));
        }
    }

    return items.at(0).substr(0, prefix);
}

vector<STRING> leftify(const vector<STRING> &items) {
    vector<STRING> r;
    for (size_t i = 0; i < items.size(); i++) {
        r.push_back(left(items.at(i)));
    }
    return r;
}

pair<STRING, size_t> extract_to(STRING curdir, STRING curfile) {
    if (restorelist.size() == 0) {
        return make_pair(curdir, 0);
    }

    STRING curdir_case = CASESENSE(curdir);
    curfile = CASESENSE(curfile);

    STRING p = parent_path(restorelist);
    size_t prefix = p.size();

    for (uint32_t i = 0; i < restorelist.size(); i++) {
        if (curdir_case == restorelist.at(i)) {
            return make_pair(curdir.substr(prefix), i);
        }

        if (curdir_case.substr(0, restorelist.at(i).size() + 1) == restorelist.at(i) + DELIM_STR) {
            return make_pair(curdir.substr(prefix), i);
        }

        if (curdir_case + DELIM_STR + curfile == restorelist.at(i)) {
            return make_pair(curdir.substr(left(p).size()), i);
        }

        if (curdir_case == L("") && curfile == restorelist.at(i)) {
            return make_pair(curdir, i);
        }
    }
    return std::make_pair(L(":"), 0);
}

void verify_restorelist(vector<STRING> restorelist, const vector<contents_t> &content) {
    STRING curdir = L("");
    contents_t c;
    for (uint32_t i = 0; i < content.size(); i++) {
        if (restorelist.size() == 0) {
            break;
        }

        c = content.at(i);
        if (c.directory) {
            if (c.name != L("./")) { // todo, simplify by not making an archive possible to contain ./ ?
                curdir = remove_delimitor(c.name);
            }
        } else {
            pair<STRING, size_t> p = extract_to(curdir, c.name);
            STRING s = p.first;
        }

        pair<STRING, size_t> p = extract_to(curdir, c.name);
        size_t j = p.second;
        STRING s = p.first;
        if (s != L(":")) // : means don't extract
        {
            restorelist.at(j) = L(":");
        }
    }

    for (uint32_t i = 0; i < restorelist.size(); i++) {
        abort(restorelist.at(i) != L(":"), L("'%s' does not exist in archive or is included multiple times by your [files] list"), restorelist.at(i).c_str());
    }
}

void force_overwrite(const STRING &file) {
    if (file != L("-stdout") && exists(file)) {
        abort(!force_flag, L("Destination file '%s' already exists"), file.c_str());
        try {
            std::filesystem::remove(file);
        } catch (std::exception &) {
            abort(true, L("Failed to overwrite file: %s"), file.c_str());
        }
    }
}

FILE *create_file(const STRING &file) {
    force_overwrite(file);
    FILE *ret = try_open(file.c_str(), 'w', true);
    return ret;
}



// todo, namespace is a temporary fix to separate things



namespace restore { 

void set_meta(STRING item, contents_t c) {
    set_date(item, c.file_modified);

    if (WIN == c.windows) {

#if _WIN32
        set_attributes(item, c.attributes);
#else
        if (!c.symlink) {
            // FIXME, check what systems don't fix to 0777 and use no-follow chmod() there
            set_attributes(item, c.attributes);        
        }
#endif

#ifdef _WIN32
        if (acl_flag) {
            bool b = set_acl(item, c.xattr_acl);
            if (!b) {
                abort(!continue_flag, L("Failed to restore ACLs for %s"), item.c_str());
                statusbar.print(2, L("Failed to restore ACLs for %s"), item.c_str());
            }
        }
#else
        if (!xattr_pattern.empty()) {
            std::string fails;
            int r = set_xattr(item, xattr_pattern, c.xattr_acl, fails);
            rassert(r != 3);
            abort(r == 2, L("Archive corrupted"));
            if (r == 1) {
                abort(!continue_flag, L("Failed to restore xattr for %s: %s"), item.c_str(), fails.c_str());
                statusbar.print(2, L("Failed to restore xattr for %s: %s"), item.c_str(), fails.c_str());
            }
        }
#endif
    } else {
        // cannot set linux xattr on windows and vice versa
    }

}

void create_symlink(STRING path, contents_t c) {
    force_overwrite(path);
#ifdef _WIN32
    int ret = CreateSymbolicLink(path.c_str(), c.link.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE | (c.directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0));
    if (ret == 0) {
        int e = GetLastError();
        abort(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, L("Plase run eXdupe as administrator to restore symlinks: %s -> %s"), path.c_str(), c.link.c_str());
        abort(true, L("Unknown error (GetLastError() = %d) restoring symlink: %s -> %s"), e, path.c_str(), c.link.c_str());
    }
    ret = 0;

#else
    int ret = symlink(c.link.c_str(), path.c_str());
#endif
    set_meta(path, c);
    abort(ret != 0, L("Error creating symlink: %s -> %s"), path.c_str(), c.link.c_str());
}

void ensure_relative(const STRING &path) {
    STRING s = STRING(L("Archive contains absolute paths. Add a [files] argument. ")) + STRING(incremental ? STRING() : STRING());
    // TODO: Not the best method
#ifdef _WIN32
    bool b = (path.size() >= 2 && path.substr(0, 2) == L("\\\\")) || path.find_last_of(L(":")) != string::npos;
#else
    bool b = (path.size() >= 2 && path.substr(0, 2) == L("\\\\"));
#endif
    abort(b, s.c_str());
}

void restore_from_file(FILE *ffull, uint64_t backup_set_number) {
    abort(backup_set_number >= sets.size(), L("Backup set does not exist"));
    bool pipe_out = directory == L("-stdout");
    std::vector<char> restore_buffer(RESTORE_CHUNKSIZE, 'c');

    if (!exists(directory)) {
        create_directories(directory, 0);
    }

    contents_t c;
    uint64_t resolved = 0;

    STRING curdir = L("");
    STRING base_dir = abs_path(directory);
    statusbar.m_base_dir = base_dir;

    vector<contents_t> dir_meta;
    vector<contents_t> content;

    for (uint32_t i = 0; i < restorelist.size(); i++) {
        restorelist.at(i) = restorelist.at(i);
        restorelist.at(i) = remove_delimitor(restorelist.at(i));
        restorelist.at(i) = CASESENSE(restorelist.at(i));
    }

    uint64_t backup_set_offset = sets.at(backup_set_number);

    basepay = read_chunks(ffull); // fixme basepay still needed?

   // verify_restorelist(restorelist, content); 
    time_ms_t d;
    uint64_t s;
    uint64_t f;
    read_backup_set(ffull, backup_set_offset, d, s, f, &backup_set, nullptr);

    read_content_map(ffull);

    for (uint32_t i = 0; i < backup_set.size(); i++) {
        uint64_t id = backup_set.at(i);
        c = content_map[id]; // get_contents_from_id(contents, id);

        if (c.directory && !c.symlink) {
            curdir = remove_delimitor(c.name);
        }

        pair<STRING, size_t> p = extract_to(curdir, c.name);
        STRING s = p.first;
        if (s != L(":")) // : means don't extract
        {
            STRING dstdir; 
            STRING x = s;

            ensure_relative(x);

            if (x.substr(0, 1) != L("\\") && x.substr(0, 1) != L("/")) {
                dstdir = remove_delimitor(base_dir + DELIM_STR + x) + DELIM_STR;
            } else {
                dstdir = remove_delimitor(base_dir + x) + DELIM_STR;
            }

            if (!pipe_out) {
                create_directories(dstdir, c.file_modified);
            }

            if (!pipe_out) {
                //save_directory(L(""), abs_path(dstdir));
            }

            if (c.directory && !c.symlink) {
                c.extra2 = abs_path(dstdir);
                dir_meta.push_back(c);
            }

            if (c.symlink) {
                files++;
                update_statusbar_restore(c.name + L(" -> ") + c.link);
                create_symlink(dstdir + c.name, c);
            } else if (!c.directory) {
                files++;
                checksum_t t;
                checksum_init(&t, hash_seed, use_aesni);
                STRING outfile = remove_delimitor(abs_path(dstdir)) + DELIM_STR + c.name;
                update_statusbar_restore(outfile);
                ofile = pipe_out ? stdout : create_file(outfile);
                resolved = 0;

                while (resolved < c.size) {
                    size_t process = minimum(c.size - resolved, RESTORE_CHUNKSIZE);
                    resolve(c.payload + resolved, process, restore_buffer.data(), ffull);
                    checksum(restore_buffer.data(), process, &t);
                    io.write(restore_buffer.data(), process, ofile);
                    update_statusbar_restore(outfile);
                    resolved += process;
                }
                if (!pipe_out) {
                    fclose(ofile);
                    set_meta(dstdir + DELIM_STR + c.name, c);
                }
                
                abort(c.hash != t.result(), retvals::err_other, format(L("File checksum error {}"), c.name));
            }
        }
    }
    for (auto &c : dir_meta) {
        set_meta(c.extra2, c);
    }
}

uint64_t payload_written = 0;
uint64_t add_file_payload = 0;
uint64_t curfile_written = 0;
checksum_t decompress_checksum;
vector<contents_t> file_queue;

void data_chunk_from_stdin(vector<contents_t> &c) {
    STRING destfile;
    STRING last_file = L("");
    uint64_t payload_orig = payload_written;
    size_t len2;
    vector<char> buf;
    auto packets = get_packets(ifile, c.at(0).payload, buf);
    
    vector<char> chunkdata;

    for (auto p : packets) {
        size_t len = p.payload_length;
        payload_orig = c.at(0).payload;
        vector<char> out(len);
        if (!p.is_reference) {
            memcpy(out.data(), p.literals, p.payload_length);
        } else if (p.is_reference) {
            uint64_t payload = p.payload_reference.value();
            // reference into a past written file
            size_t resolved = 0;
            while (resolved < len) {
                if (payload + resolved >= payload_orig) {
                    size_t fo = belongs_to(payload + resolved);
                    int j = io.seek(ofile, payload + resolved - payload_orig, SEEK_SET);
                    massert(j == 0, "Internal error or destination drive is not seekable", infiles.at(fo).filename, payload, payload_orig);
                    len2 = io.read_vector(out, len - resolved, resolved, ofile, false);
                    massert(!(len2 != len - resolved), "Internal error: Reference points past current output file", infiles.at(fo).filename, len, len2);
                    resolved += len2;
                    io.seek(ofile, 0, SEEK_END);
                } else {
                    FILE *ifile2;
                    size_t fo = belongs_to(payload + resolved);
                    {
                        ifile2 = try_open(infiles.at(fo).filename, 'r', true);
                        infiles.at(fo).handle = ifile2;
                        int j = io.seek(ifile2, payload + resolved - infiles.at(fo).offset, SEEK_SET);
                        massert(j == 0, "Internal error or destination drive is not seekable", infiles.at(fo).filename, payload, infiles.at(fo).offset);
                    }
                    // FIXME only request to read exact amount, so that we can call with read_exact = true
                    len2 = io.read_vector(out, len - resolved, resolved, ifile2, false);
                    resolved += len2;
                    fclose(ifile2);
                }
            }
        }
        chunkdata.insert(chunkdata.end(), out.begin(), out.end());
    }

    uint64_t src_consumed = 0;

    while (c.size() > 0 && src_consumed < chunkdata.size()) {
        if (ofile == 0) {
            ofile = create_file(c.at(0).extra);
            destfile = c.at(0).extra;
            checksum_init(&decompress_checksum, hash_seed, use_aesni);
            {
                file_offset_t t;
                t.filename = c.at(0).extra;
                t.offset = add_file_payload;
                t.handle = 0;
                add_file_payload += c.at(0).size;
                infiles.push_back(t);
            }
            curfile_written = 0;
        }
        auto missing = c.at(0).size - curfile_written;
        auto has = minimum(missing, chunkdata.size() - src_consumed);

        curfile_written += has;
        if (verbose_level < 3) {
            // level 3 doesn't show progress
            update_statusbar_restore(destfile);
        }

        io.write(&chunkdata[src_consumed], has, ofile);

        checksum(&chunkdata[src_consumed], has, &decompress_checksum);
        payload_written += has;
        src_consumed += has;

        if (curfile_written == c.at(0).size) {
            io.close(ofile);
            set_meta(c.at(0).extra, c.at(0));
            ofile = 0;
            curfile_written = 0;
            abort(c.at(0).hash != decompress_checksum.result(), retvals::err_other, format(L("File checksum error {}"), c.at(0).extra));
            c.erase(c.begin());
        }
    }
    
}


void restore_from_stdin(const STRING& extract_dir) {
    STRING curdir;
    size_t r = 0;
    STRING base_dir = abs_path(extract_dir);
    statusbar.m_base_dir = base_dir;

    if (!exists(extract_dir)) {
        create_directories(extract_dir, 0);
    }

    curdir = extract_dir;
    // ensure_relative(curdir);
    save_directory(L(""), curdir + DELIM_STR, false, {}); // initial root

    vector<contents_t> identicals_queue;
    std::map<uint64_t, STRING> written;
    std::vector<contents_t> dir_meta;

    for (;;) {
        char w;

        r = io.read(&w, 1, ifile);
        abort(r == 0, L("Unexpected end of archive (block tag)"));

        if (w == 'I') {
            contents_t c;
            read_content_item(ifile, c);
            ensure_relative(c.name);
            curdir = extract_dir + DELIM_STR + c.name;
            save_directory(L(""), curdir, false, {});
            create_directories(curdir, c.file_modified);
            c.extra2 = abs_path(curdir);
            dir_meta.push_back(c);
        }
        else if (w == 'U') {
            contents_t c;
            files++;
            read_content_item(ifile, c);
            STRING buf2 = remove_delimitor(curdir) + DELIM_STR + c.name;
            c.extra = buf2;
            identicals_queue.push_back(c);
        }
        else if (w == 'F') {
            contents_t c;
            files++;
            read_content_item(ifile, c);
            STRING buf2 = remove_delimitor(curdir) + DELIM_STR + c.name;

            if (c.size == 0) {
                // May not have a corresponding data chunk ('A' block) to trigger decompress_files()
                FILE* h = create_file(buf2);
                files++;
                io.close(h);
                set_meta(buf2, c);
            }
            else {
                c.extra = buf2;
                file_queue.push_back(c);
                written.insert({ c.file_id, c.extra });
                update_statusbar_restore(buf2);
                name = c.name;
            }
        } else if (w == 'A') {
            data_chunk_from_stdin(file_queue);
        }
        else if (w == 'C') { // crc
            auto &arr = file_queue.at(file_queue.size() - 1).hash;
            io.read(arr.data(), sizeof(arr), ifile);
        }
        else if (w == 'L') { // symlink
            contents_t c;
            files++;
            read_content_item(ifile, c);
            STRING buf2 = curdir + DELIM_CHAR + c.name;
            create_symlink(buf2, c);
        }

        else if (w == 'X') {
            if (file_queue.size() > 0 && curfile_written > 0) {
                // archive was created from -stdin with non-zero sized input
                abort(file_queue.at(0).hash != decompress_checksum.result(), retvals::err_other, format(L("File checksum error {}"), file_queue.at(0).extra));
            }
            break;
        }
        else {
            abort(true, L("Source file corrupted"));
        }
    }

    vector<char> buf;
    buf.resize(DISK_READ_CHUNK);
    for (auto& i : identicals_queue) {
        auto dst = i.extra;
        auto r = written.find(i.duplicate);
        auto src = r->second;

        auto ofile = create_file(dst);
        auto ifile = try_open(src, 'r', true);
        for (size_t r; (r = io.read(buf.data(), DISK_READ_CHUNK, ifile, false));) {
            io.write(buf.data(), r, ofile);
            // fixme dates?
            update_statusbar_restore(dst);
        }
        io.close(ifile);
        io.close(ofile);
        set_meta(dst, i);
    }

    for (auto &c : dir_meta) {
        set_meta(c.extra2, c);
    }
}

} // namespace decompression

void compress_symlink(const STRING &link, const STRING &target, attr_t a) {
    bool is_dir;
    STRING tmp;

    bool ok = symlink_target(link.c_str(), tmp, is_dir);

    if (!ok) {
        if (continue_flag) {
            statusbar.print(2, L("Skipped, error by readlink(): %s"), link.c_str());
        } else {
            abort(true, L("Aborted, error by readlink(): %s"), link.c_str());
        }
        return;
    }

    update_statusbar_backup(link + L(" -> ") + STRING(tmp));
    io.write("L", 1, ofile);

    files++;

    filetimes times = get_date(link);

    contents_t c;
    c.directory = is_dir;
    c.symlink = true;
    c.link = STRING(tmp);
    c.name = target;
    c.size = 0;
    c.payload = 0;
    c.file_modified = times.written;
    c.file_c_time = times.created;
    c.file_change_time = times.changed;
    c.file_id = file_id_counter++;
    
    c.attributes = a.attr;
    c.xattr_acl = a.xattr;

    write_contents_item(ofile, c);
    
    contents.push_back(c);
    contents_added.push_back(c);

    backup_set.push_back(c.file_id);
    
    return;
}

// todo, namespace is a temporary fix to separate things
namespace compression {

uint64_t payload_compressed = 0; // Total payload returned by dup_compress() and flush_pend()
uint64_t payload_read = 0;       // Total payload read from disk

std::vector<std::vector<char>> payload_queue; // Queue of payload read from disk. Can contain multiple small files
std::vector<size_t> payload_queue_size;
size_t current_queue = 0;

std::vector<std::vector<char>> out_payload_queue;
std::vector<size_t> out_payload_queue_size;
size_t out_current_queue = 0;

vector<contents_t> file_queue;
std::mutex compress_file_mutex;

checksum_t file_meta_ct;

vector<char> dummy(DISK_READ_CHUNK);

void empty_q(bool flush, bool entropy) {
    uint64_t pay;
    size_t cc;
    char* out_result;

    auto write_result = [&]() {
        if (cc > 0) {
            io.write("A", 1, ofile);
            auto p = io.tell(ofile);
            chunk_t c;
            c.payload_length = pay;
            c.compressed_length = cc;
            c.archive_offset = p;
            c.payload = pay_count;
            pay_count += pay;
            chunks.push_back(c);
            chunks_added.push_back(c);
            io.write(out_result, cc, ofile); 
        }
        payload_compressed += pay;
    };

    if (payload_queue_size[current_queue] > 0) {
        cc = dup_compress(payload_queue[current_queue].data(), out_payload_queue[out_current_queue].data(), payload_queue_size[current_queue], &pay, entropy, out_result);
        abort(cc == dup_err_malloc, retvals::err_memory, "Out of memory. Reduce -m, -g or -t flag");
        write_result();
        current_queue = (current_queue + 1) % out_payload_queue.size();
        payload_queue_size[current_queue] = 0;
        out_current_queue = (out_current_queue + 1) % out_payload_queue.size();;
    }

    if (flush) {
        while (payload_compressed < payload_read) {
            cc = flush_pend(&pay, out_result);
            write_result();
        }
    }
}

void compress_file_finalize() {
    empty_q(true, false);
}
using namespace std;
void compress_file(const STRING& input_file, const STRING& filename, attr_t attributes) {
    update_statusbar_backupv3(input_file);

    if (input_file != L("-stdin") && ISNAMEDPIPE(attributes.attr) && !named_pipes) {
        auto _ = std::lock_guard(compress_file_mutex);
        statusbar.print(2, L("Skipped, no -p flag for named pipes: %s"), input_file.c_str());
        return;
    }

    filetimes file_time = input_file == L("-stdin") ? filetimes(cur_date(), cur_date(), cur_date()) : get_date(input_file);

    checksum_t file_checksum;
    checksum_init(&file_checksum, hash_seed, use_aesni);
    uint64_t file_size = 0;
    contents_t file_meta;
    uint64_t file_read = 0;   

#if 1
    // Detect files that are unchanged between full and diff backup, by comparing created and last-modified timestamps
    //
    // TODO: With no_timestamp_flag, an untouched file (all timestamps and all contents identical) will be caught in the
    // identical_files check later, but that will add a duplicated contents_t entry which is superfluous. Fix this by
    // either making untouched_files check contents (maybe most clean solution?) or by making identical_files able to 
    // search for and reuse the contents_t.
    if (!no_timestamp_flag && incremental && input_file != L("-stdin")) {
        auto c = untouched_files2.exists(input_file, filename, file_time);
        if(c) {
            auto _ = std::lock_guard(compress_file_mutex);
            update_statusbar_backup(input_file);
            unchanged += c->size;
            unchanged_files++;
           // contents.push_back(*c);
            backup_set.push_back(c->file_id);
            files++;
            return;
        }
    }
#endif

    auto error_reading = [&]() {
        auto _ = std::lock_guard(compress_file_mutex);
        if (continue_flag) {
            statusbar.print(2, L("Skipped, error reading source file: %s"), input_file.c_str());
        }
        else {
            abort(true, L("Aborted, error reading source file: %s"), input_file.c_str());
        }
    };

    FILE* handle = try_open(input_file.c_str(), 'r', false);

    if (!handle) {
        error_reading();
        return;
    }

    if (input_file != L("-stdin")) {
        update_statusbar_backup(input_file);
        // Initial read is slow, so we read DISK_READ_CHUNK concurrently (outside compress_file_mutex)
        size_t prefetch = DISK_READ_CHUNK;
        size_t r = io.read(dummy.data(), prefetch, handle, false);
        io.seek(handle, 0, SEEK_END);
        file_size = io.tell(handle);
        // fread() can fail on Windows even if the file is opened successfully for reading
        if(r < minimum(file_size, prefetch)) {
            fclose(handle);
            error_reading();
            return;
        }
        io.seek(handle, 0, SEEK_SET);
    } else {
        file_size = std::numeric_limits<uint64_t>::max();
    }
    
    file_meta.abs_path = abs_path(input_file);
    file_meta.name = filename;
    file_meta.size = file_size;
    
    file_meta.file_c_time = file_time.created;
    file_meta.file_modified = file_time.written;
    file_meta.file_change_time = file_time.changed;

    file_meta.attributes = attributes.attr;
    file_meta.directory = false;
    file_meta.symlink = false;
    file_meta.xattr_acl = attributes.xattr;

    // compress_file() now synchronized
    auto _ = std::lock_guard(compress_file_mutex);

    file_meta.payload = payload_read + basepay;
    file_meta.file_id = file_id_counter++;

    files++;

#if 1 // Detect files with identical payload, both within current backup set, and between full and diff sets
    if(file_size >= IDENTICAL_FILE_SIZE && input_file != L("-stdin")) {
        auto original = identical;
        auto cont = identical_files.identical_to(handle, file_meta, io, [](uint64_t n, const STRING& file) { identical += n; update_statusbar_backup(file); }, input_file, hash_seed, use_aesni);

        if(cont.has_value()) {
            file_meta.payload = cont.value().payload;
            file_meta.hash = cont.value().hash;
            file_meta.duplicate = cont.value().file_id;

            if (!incremental) {
                // todo clear abs_path?
                io.write("U", 1, ofile);
                write_contents_item(ofile, file_meta);
            }

            identical_files_count++;

            contents.push_back(file_meta);
            contents_added.push_back(file_meta);
            backup_set.push_back(file_meta.file_id);

            io.close(handle);
            return;            
        }
        else {
            identical = original;
        }
    }
#endif

    checksum_init(&file_meta_ct, hash_seed, use_aesni);

    if(!incremental) {
        io.write("F", 1, ofile);
        contents_t tmp = file_meta;
        tmp.abs_path.clear(); // todo why is this cleared?
        write_contents_item(ofile, tmp);
    }

    file_queue.push_back(file_meta);
    bool entropy = false;
    io.seek(handle, 0, SEEK_SET);
    
    // todo, simplify - this flag may not be needed
    bool overflows = file_size > DISK_READ_CHUNK - payload_queue_size[current_queue];

    if (overflows) {
        empty_q(false, entropy);
        if (file_size >= IDENTICAL_FILE_SIZE) {
            entropy = file_types.high_entropy(0, filename);
            if(entropy) {
                high_entropy_files++;
            }
        }
    }

    while (file_read < file_size) {
        update_statusbar_backup(input_file);

        size_t read = minimum(file_size - file_read, DISK_READ_CHUNK);
        size_t r = io.read_vector(payload_queue[current_queue], read, payload_queue_size[current_queue], handle, false);
        abort(io.stdin_tty() && r != read, (L("Unexpected midway read error, cannot continue: ") + input_file).c_str());
        checksum(payload_queue[current_queue].data() + payload_queue_size[current_queue], r, &file_meta_ct);

        payload_queue_size[current_queue] += r;
        file_read += r;
        payload_read += r;

        if ((overflows && input_file == L("-stdin") && r == 0) || (file_read == file_size && file_size > 0)) {
            // No CRC block for 0-sized files
            if (file_read > 0) {
                io.write("C", 1, ofile);
                file_meta.hash = file_meta_ct.result();
                io.write(file_meta_ct.result().data(), sizeof(file_meta.hash), ofile);
            }
            if ((overflows && input_file == L("-stdin") && r == 0)) {
                break;
            }
        }

        if (overflows && file_read >= file_size) {
            entropy = false;
        }
        
        if(overflows) {
            empty_q(false, entropy);
        }
    }

    if(overflows) {
        file_queue.clear();
    }

    fclose(handle);

    if (input_file == L("-stdin")) {
        file_meta.size = file_read;
    }

    file_meta.hash = file_meta_ct.result();
    identical_files.add(file_meta);

    contents.push_back(file_meta); // fixme, use method that guarantees we push to both vectors
    contents_added.push_back(file_meta);

    backup_set.push_back(file_meta.file_id);
}

} // namespace compression

bool lua_test(STRING path, const STRING &script, bool top_level) {
    if (script == L("")) {
        return true;
    }

    STRING dir;
    STRING file;
    uint64_t size = 0;
    STRING ext;
    STRING name;
    uint32_t attrib = 0;
    time_ms_t date;
    int type;

#ifdef _WIN32
    // todo reuse utilities attrib function
    HANDLE hFind;
    WIN32_FIND_DATAW data;
    hFind = FindFirstFileW(path.c_str(), &data);
    attrib = data.dwFileAttributes;

    if (hFind != INVALID_HANDLE_VALUE) {
        FindClose(hFind);
    }

#endif

    type = is_symlink(path) ? SYMLINK_TYPE : is_dir(path) ? DIR_TYPE : FILE_TYPE;
    date = get_date(path).written;
    path = remove_delimitor(path);
    name = right(remove_delimitor(path)) == L("") ? path : right(remove_delimitor(path));
    size = filesize(path, false);

    size_t t = name.find_last_of(L("."));
    if (t != string::npos) {
        ext = name.substr(t + 1);
    } else {
        ext = L("");
    }
    return execute(script, path, type, name, size, ext, attrib, date, top_level);
}

bool include(const STRING &name, bool top_level) {
    STRING n = remove_delimitor(CASESENSE(unsnap(abs_path(name))));

    for (uint32_t j = 0; j < excludelist.size(); j++) {
        STRING e = excludelist.at(j);
        if (n == e) {
            // statusbar.print(9, L("Skipped, in -- exclude list: %s"), name.c_str());
            return false;
        }
    }

    if (!lua_test(name, lua, top_level)) {
        // statusbar.print(9, L("Skipped, by -f filter: %s"), name.c_str());
        return false;
    }
    return true;
}

void fail_list_dir(const STRING &dir) {
    if (continue_flag) {
        statusbar.print(2, L("Skipped, error listing directory: %s"), dir.c_str());
    } else {
        abort(true, L("Aborted, error listing directory: %s"), dir.c_str());
    }
}

void compress_recursive(const STRING &base_dir, vector<STRING> items2, bool top_level) {

    using item = pair<STRING, attr_t>;

    vector<item> files;
    vector<item> symlinks;
    vector<item> directories;

    std::sort(items2.begin(), items2.end(), [](STRING a, STRING b) { return a.find(DELIM_STR) == string::npos && b.find(DELIM_STR) != string::npos; });

    for (auto& item : items2) {
        STRING sub = base_dir + item;
        int type = get_attributes(sub, follow_symlinks);

        if (type == -1) {
            if (continue_flag) {
                statusbar.print(2, L("Skipped, access error: %s"), sub.c_str());
            } else {
                abort(true, L("Aborted, access error: %s"), sub.c_str());
            }
        } else {
#ifdef _WIN32
            if (follow_symlinks && ISLINK(type) && !is_symlink_consistent(sub)) {
                if (continue_flag) {
                    statusbar.print(2, L("Skipped, symlink has SYMLINK/SYMLINKD mismatch: %s"), sub.c_str());
                } else {
                    abort(true, L("Aborted, symlink has SYMLINK/SYMLINKD mismatch: %s"), sub.c_str());
                }
                continue;
            }
#endif

            std::string xattr_acl;
#ifdef _WIN32
            if (acl_flag) {
                bool b = get_acl(sub, xattr_acl, follow_symlinks);
                if (!b && continue_flag) {
                    statusbar.print(2, L("Skipped, error reading ACLs for: %s"), sub.c_str());
                } 
                else if (!b) {
                    abort(true, L("Aborted, error reading ACLs for: %s"), sub.c_str());
                }
            }
#else
            if (!xattr_pattern.empty()) {
                bool b = get_xattr(sub, xattr_acl, xattr_pattern, follow_symlinks);
                if (!b && continue_flag) {
                    statusbar.print(2, L("Skipped, error reading xattr for: %s"), sub.c_str());
                } else if (!b) {
                    abort(true, L("Aborted, error reading xattr for: %s"), sub.c_str());
                }
            }
#endif

            attr_t a = {type, xattr_acl};

            // avoid including archive itself when compressing
            if ((output_file == L("-stdout") || !same_path(sub, full)) && include(sub, top_level)) {
                if ((!ISDIR(type) && !ISSOCK(type)) && !(ISLINK(type) && !follow_symlinks)) {
                    files.emplace_back(item, a);
                }
                else if(ISLINK(type) && !follow_symlinks) {
                    symlinks.emplace_back(item, a);
                }
                else if(ISDIR(type) && (!no_recursion_flag || top_level)) {
                    directories.emplace_back(item, a);
                }
            }
        }
    }


    // First process files
    std::atomic<size_t> ctr = 0;
    const int max_threads = 1;
    std::thread threads[max_threads];
    std::atomic<bool> abort_flag = false;
    std::exception_ptr thread_exc = nullptr;
    std::mutex thread_exc_mutex;

    auto compress_file_function = [&]() {
        size_t j = ctr.fetch_add(1);
        while (!abort_flag && j < files.size()) {
            rassert(j < files.size());
            STRING sub = base_dir + files.at(j).first;
            STRING L = files.at(j).first;
            STRING s = right(L) == L("") ? L : right(L);

            try {
                compression::compress_file(sub, s, files.at(j).second);
            } catch (...) {
                std::lock_guard<std::mutex> lg(thread_exc_mutex);
                thread_exc = std::current_exception();
                abort_flag = true;
                return;
            }
            j = ctr.fetch_add(1);
        }
    };

    if(files.size() > 1) {
        size_t thread_count = minimum(files.size(), max_threads);
        for (size_t t = 0; t < thread_count; t++) {
            threads[t] = std::thread(compress_file_function);
        }
        for (size_t t = 0; t < thread_count; t++) {
            threads[t].join();
        }
    }
    else {
        compress_file_function();
    }

    if (abort_flag) {
        std::rethrow_exception(thread_exc);
    }

    // then process symlinks (if followed, they will be contained in the files list above instead of here)
    if (!follow_symlinks) {
        for (auto& symlink : symlinks) {
            STRING sub = base_dir + symlink.first;
            // FIXME, why this?
            //save_directory(base_dir, left(symlink.first) + (left(symlink.first) == L("") ? L("") : DELIM_STR), true, {});
            compress_symlink(sub, right(symlink.first) == L("") ? symlink.first : right(symlink.first), symlink.second);
        }
    }

    // finally process directories
    for (auto& dir : directories) {
        if (dir.first != L("")) {
            dir.first = remove_delimitor(dir.first) + DELIM_STR;
        }
        STRING sub = base_dir + dir.first;
        if (!no_recursion_flag || top_level) {
            vector<STRING> newdirs;
#ifdef _WIN32
            HANDLE hFind;
            BOOL bContinue = TRUE;
            WIN32_FIND_DATAW data;
            STRING s = remove_delimitor(sub) + L("\\*");
            hFind = FindFirstFileW(s.c_str(), &data);
            bContinue = hFind != INVALID_HANDLE_VALUE;

            if (hFind == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) {
                fail_list_dir(sub);
            } else {
                while (bContinue) {
                    if (STRING(data.cFileName) != L(".") && STRING(data.cFileName) != L("..")) {
                        newdirs.push_back(dir.first + STRING(data.cFileName));
                    }
                    bContinue = FindNextFileW(hFind, &data);
                }
                FindClose(hFind);
            }
#else
            struct dirent *entry;
            DIR *dir2 = opendir(sub.c_str());

            if (dir2 == 0) {
                fail_list_dir(sub);
            } else {
                while ((entry = readdir(dir2)) != 0) {
                    if (STRING(entry->d_name) != L(".") && STRING(entry->d_name) != L("..")) {
                        newdirs.push_back(dir.first + entry->d_name);
                    }
                }
                closedir(dir2);
            }
#endif
            if (dir.first != L("")) {
                dirs++;
            }
            save_directory(base_dir, dir.first, true, dir.second);
            compress_recursive(base_dir, newdirs, false);
        }
    }
}

void compress(const STRING &base_dir, vector<STRING> items) {
    compress_recursive(base_dir, items, true);
    compression::compress_file_finalize();
}

void compress_args(vector<STRING> args) {
    uint32_t i = 0;
    for (i = 0; i < args.size(); i++) {
        args.at(i) = remove_leading_curdir(args.at(i));
        if (is_dir(args.at(i)) && !is_symlink(args.at(i))) {
            args.at(i) = remove_delimitor(args.at(i)) + DELIM_STR;
        }
    }

    size_t prefix = longest_common_prefix(args, !WIN);
    STRING base_dir = args.at(0).substr(0, prefix);

    base_dir = left(base_dir);
    if (base_dir != L("")) {
        base_dir += DELIM_CHAR;
    }
    statusbar.m_base_dir = base_dir;

    for (i = 0; i < args.size(); i++) {
        args.at(i) = args.at(i).substr(base_dir.length());
    }

    compress(base_dir, args);
}

void print_statistics(uint64_t start_time, uint64_t end_time, uint64_t end_time_without_overhead, uint64_t references_size, uint64_t hashtable_size, uint64_t added) {
    std::ostringstream s;
    int sratio = int((double(added) / double(backup_set_size() + 1)) * 100.);
    sratio = sratio > 999 ? 999 : sratio == 0 ? 1 : sratio;

    s << "Input:                       " << w2s(del(backup_set_size())) << " B in " << w2s(del(files)) << " files\n";
    s << "Output:                      " << w2s(del(added)) << " B (" << sratio << "%)\n";
    s << "Speed:                       " << w2s(del(backup_set_size() / ((end_time - start_time) + 1) * 1000 / 1024 / 1024)) << " MB/s\n";
    s << "Speed w/o init overhead:     " << w2s(del(backup_set_size() / ((end_time_without_overhead - start_time_without_overhead) + 1) * 1000 / 1024 / 1024)) << " MB/s\n";

    if (incremental) {
        s << "Stored as untouched files:   " << suffix(unchanged) << "B in " << w2s(del(unchanged_files)) << " files\n";
    }
    s << "Stored as duplicated files:  " << suffix(identical) << "B in " << w2s(del(identical_files_count)) << " files\n";
    s << "Stored as duplicated blocks: " << suffix(largehits + smallhits) << "B (" << suffix(largehits) << "B large, " << suffix(smallhits) << "B small)\n";
    s << "Stored as literals:          " << suffix(stored_as_literals) << "B (" << suffix(literals_compressed_size) << "B compressed)\n";
    // uint64_t total = literals_compressed_size + contents_size + references_size + hashtable_size; // fixme
    s << "Overheads:                   " << suffix(contents_size) << "B meta, " << suffix(references_size) << "B refs\n";
    s << "Hashtable:                   " << suffix(hashtable_size) << "B\n";//    << suffix(io.write_count - total) << " B misc\n ";
    s << "Unhashed anomalies:          " << suffix(anomalies_large) << "B large, " << suffix(anomalies_small) << "B small\n";
    s << "High entropy files:          " << suffix(high_entropy) << "B in " << w2s(del(high_entropy_files)) << " files";
    STRING str = s2w(s.str());
    statusbar.print(0, L("%s"), str.c_str());
    CERR << "Hashtable fillratio:         ";
    double la = 0.;
    double sm = 0.;
    fillratio(&la, &sm);
    CERR << int(sm * 100.) << "% small, " << int(la * 100.) << "% large\n";
    if (VER_DEV != 0) {
        CERR << "\nhits1 = " << hits1 << "";
        CERR << "\nhits2 = " << hits2 << "\n";
        CERR << "hits3 = " << hits3 << "\n";
        CERR << "hits4 = " << hits4 << "\n";
    }
}


void wrote_message(uint64_t bytes, uint64_t files) { statusbar.print(1, L("Wrote %s bytes in %s files"), del(bytes).c_str(), del(files).c_str()); }

#ifdef _WIN32
void create_shadows(void) {
    shadow(shadows);
    vector<pair<STRING, STRING>> snaps; //(STRING mount, STRING shadow)
    snaps = get_snaps();
    for (uint32_t i = 0; i < snaps.size(); i++) {
        statusbar.print(3, L("Created snapshot %s -> %s"), snaps.at(i).first.c_str(), snaps.at(i).second.c_str());
    }
}
void remove_shadows(void) { unshadow(); }
#else
void create_shadows(void) { }
void remove_shadows(void) { }
#endif

void main_compress() {
    uint64_t lastgood = 0;
    scope_actions([]() { create_shadows(); }, []() { remove_shadows(); });
    file_types.add(entropy_ext);

    for (uint32_t i = 0; i < threads + 1; i++) {
        size_t drc = DISK_READ_CHUNK;
        compression::payload_queue.push_back(std::vector<char>(drc));
        compression::payload_queue_size.push_back(0);
        compression::out_payload_queue.push_back(std::vector<char>(dup_compressed_ubound(drc)));
        compression::out_payload_queue_size.push_back(0);
    }

    if (full != L("-stdout") && !force_flag && std::filesystem::exists(full)) {
        incremental = true;
    }

    if (incremental) {
        output_file = full;
        ifile = try_open(full.c_str(), 'a', true);
        ofile = ifile;
        if (verbose_level > 0) {
            statusbar.update(BACKUP, 0, 0, (STRING() + L("Reading metadata...\r")).c_str(), true, true);
        }
        uint64_t memory_usage_from_file = read_header(ifile, &lastgood); // also inits hash_seed and sets
        abort((gigabyte_flag || megabyte_flag) && memory_usage != memory_usage_from_file, retvals::err_other, "Skip the -m or -g flag or use the same value as during the initial backup");
        memory_usage = memory_usage_from_file;

        // todo, function names and what they return are not descriptive
        bool was_killed = !read_headers(ifile);

        hashtable = malloc(memory_usage);
        abort(!hashtable, retvals::err_memory, format("Out of memory. This incremental backup requires {} MB. Try -t1 flag", memory_usage >> 20));
        //memset(hashtable, 0, memory_usage);
        pay_count = read_chunks(ifile); // read size in bytes of user payload in .full file

        int r = dup_init(DEDUPE_LARGE, DEDUPE_SMALL, memory_usage, threads, hashtable, compression_level, hash_seed, pay_count);
        abort(r == 1, retvals::err_memory, format("Out of memory. This incremental backup requires {} MB. Try -t1 flag", memory_usage >> 20));
        abort(r == 2, retvals::err_memory, format("Error creating threads. This incremental backup requires {} MB memory. Try -t1 flag", memory_usage >> 20));

        // Accumulated payload of initial backup + all incrementals
        basepay = pay_count;
        contents = read_contents(ifile);
        for (auto c : contents) {
            c.abs_path = CASESENSE(c.abs_path);
            untouched_files2.add_during_backup(c);
            identical_files.add(c);
        }

        if (!was_killed) {
            io.seek(ifile, 0, SEEK_END);
            original_file_size = io.tell(ifile);
            read_hashtable(ifile);
            seek_to_header(ifile, hashtable_header);
            io.seek(ifile, -8, SEEK_CUR);
            io.truncate(ifile);
        } else {
            // eXdupe was killed during last incremental backup
            io.seek(ifile, lastgood, SEEK_SET);
            io.truncate(ifile);
            original_file_size = io.tell(ifile);
        }

    } else {
        output_file = full;
        ofile = create_file(output_file);
        hash_seed = static_cast<uint32_t>(rnd64());
        hashtable = tmalloc(memory_usage);
        abort(!hashtable, retvals::err_memory, "Out of memory. Reduce -m, -g or -t flag");
        int r = dup_init(DEDUPE_LARGE, DEDUPE_SMALL, memory_usage, threads, hashtable, compression_level, hash_seed, 0);
        abort(r == 1, retvals::err_memory, "Out of memory. Reduce -m, -g or -t flag");
        abort(r == 2, retvals::err_memory, "Error creating threads. Reduce -m, -g or -t flag");
        write_header(ofile, memory_usage, hash_seed, 0);
    }

    auto commit = [&]() {
        if (output_file != L("-stdout")) {
            lastgood = io.tell(ofile);
            io.seek(ofile, 0, SEEK_SET);
            write_header(ofile, memory_usage, hash_seed, lastgood);
            io.seek(ofile, lastgood, SEEK_SET);
        }
    };

    io.write(payload_header.data(), payload_header.size(), ofile);
    uint64_t w = io.write_count;

    start_time_without_overhead = GetTickCount64();

    try {
        if (inputfiles.size() > 0 && inputfiles.at(0) != L("-stdin")) {
            compress_args(inputfiles);
        } else if (inputfiles.size() > 0 && inputfiles.at(0) == L("-stdin")) {
            name = L("stdin");
            compression::compress_file(L("-stdin"), name, {});
            compression::compress_file_finalize();
        }
    } catch (const retvals&) {
        // Thrown by abort() which has already printed the error message to the user and set aborted
    }
    catch (const std::exception &e) {
        // Some errors, like from std::filesystem, are not handled, so we catch them here.
        CERR << std::endl << L("Error: ") << e.what() << std::endl;
        aborted = static_cast<int>(retvals::err_other);
    } catch (...) {
        throw;
    }

    // If aborted, we chose to write the hashtable to the archive so that the next backup can benefit
    // from deduplication. If killed or crashed, the archive is left with no hashtable at all; it will
    // then be rebuilt gradually during the next backups.

    io.write("X", 1, ofile);

    // PAYLOADD header end length
    io.write_ui<uint64_t>(io.write_count - w, ofile);

    uint64_t end_time_without_overhead = GetTickCount64();
    size_t references_size = write_chunks_added(ofile);
    write_contents_added(ofile);

    commit();

    if (verbose_level > 0) {
        //statusbar.clear_line();
        STRING msg = aborted ? L("Aborting, please wait...\r") : L("Writing metadata...\r");
        statusbar.update(BACKUP, backup_set_size(), io.write_count, msg.c_str(), true, true);
    }

    if (!aborted) {
        time_ms_t d = cur_date();
        uint64_t s = backup_set_size();
        uint64_t f = files;
        write_backup_set(ofile, d, s, f, inputfiles);
        commit();
    }

    size_t hashtable_size = write_hashtable(ofile);

    io.write(file_footer.data(), file_footer.size(), ofile);

    if (verbose_level > 0 && verbose_level < 3) {
        statusbar.clear_line();
    }

    uint64_t added = 0;
    if (ofile != stdout) {
        io.seek(ofile, 0, SEEK_END);
        added = io.tell(ofile) - original_file_size;
        io.close(ofile);

    } else {
        added = io.write_count;
    }

    if (statistics_flag) {
        uint64_t end_time = GetTickCount64();
        print_statistics(start_time, end_time, end_time_without_overhead, references_size, hashtable_size, added);
    } else if (!aborted) {
        statusbar.print_no_lf(1, L("Added %s B in %s files using %sB\n"), del(backup_set_size()).c_str(), del(files).c_str(), s2w(suffix(added)).c_str());
    }
}


void main_restore() {
    if (full != L("-stdin")) {
        // Restore from file.
        // =================================================================================================
        if (incremental) {
            FILE *ffull = try_open(full, 'r', true);
            read_header(ffull, nullptr); // inits sets
            init_content_maps(ffull);
        } else {
            ifile = try_open(full, 'r', true);
            read_header(ifile, nullptr); // initializes sets
            read_headers(ifile);
            restore::restore_from_file(ifile, set_flag == static_cast<uint32_t>(-1) ? 0 : set_flag);
        }
        wrote_message(io.write_count, files);
    } else if ((full == L("-stdin")) && restorelist.size() == 0) {
        // fixme, only archives containing 1 set can be restored this way; add detection+error handling
        // Restore from stdin. Only entire archive can be restored this way
        STRING s = remove_delimitor(directory);
        ifile = try_open(full, 'r', true);
        read_header(ifile, nullptr);

        // seek_to_header(ifile, "PAYLOADD");
        char tmp2[8];
        io.read(tmp2, 8, ifile, true);

        restore::restore_from_stdin(s);
        rassert(!incremental);
        wrote_message(io.write_count, files);

        // read remainder of file like content section, etc, to avoid error from OS
        vector<std::byte> tmp(32 * 1024, {});
        while (ifile == stdin && io.read(tmp.data(), 32 * 1024, stdin, false)) {
        }
    }
}


#ifdef _WIN32
int wmain(int argc2, CHR *argv2[])
#else
int main(int argc2, char *argv2[])
#endif
{
    int retval = 0;
    use_aesni = dup_is_aesni_supported();

#ifdef _WIN32
    // Warning: Apparently you can only call _setmode once for a given stream, else it will assert
    // because it still expects the old char width. So only call it from main where it's visible
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stderr), _O_U8TEXT);
#endif

    try {
        tidy_args(argc2, argv2);
        parse_flags();
        statusbar.m_verbose_level = verbose_level;

        if (argc == 1) {
            statusbar.use_cerr();
            print_usage(false);
            return static_cast<int>(retvals::err_parameters);
        }
        if (usage_flag) {
            print_usage(true);
            return 0;
        }
        if (lua_help_flag) {
            print_lua_help();
            return 0;
        }
        if (e_help_flag) {
            print_e_help();
            return 0;
        }
        if (build_info_flag) {
            print_build_info();
            return 0;
        }

#ifdef _WIN32
        PrivilegeGuard pg;
        if (acl_flag) {
            // If this call fails, we will simply continue and see if we can read ACLs anyway
            pg.enable({SE_SECURITY_NAME, SE_TAKE_OWNERSHIP_NAME, SE_RESTORE_NAME, SE_BACKUP_NAME});
        }
#endif

        parse_files();

        if (directory == L("-stdout") || full == L("-stdout")) {
#ifdef _WIN32
            _setmode(_fileno(stdout), _O_BINARY);
#endif
            statusbar.use_cerr();
        }
        else {
#ifdef _WIN32
            _setmode(_fileno(stdout), _O_U8TEXT);
#endif
        }

        if (list_flag) {
            statusbar.m_verbose_level = 3;
            list_contents();
            return 0;
        } 
        
        if (restore_flag) {
            main_restore();
        } else if (compress_flag) {
            main_compress();
        }
    } 
    catch (retvals r) {
        retval = static_cast<int>(r);
    }
    catch (std::exception& e) {
        CERR << e.what();
        retval = static_cast<int>(retvals::err_std_etc);
    }
    if (aborted.load()) {
        retval = aborted.load();
    }

    return retval;
}
