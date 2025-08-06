// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2025: Lasse Mikkel Reinhold

#define VER_MAJOR 3
#define VER_MINOR 0
#define VER_REVISION 0
#define VER_DEV 0

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

#include "libexdupe/xxHash/xxh3.h"
#include "libexdupe/xxHash/xxhash.h"

#ifdef _WIN32
const bool WIN = true;
#include "shadow/shadow.h"
#include <fcntl.h>
#include <io.h>
#include <windows.h>

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

#include "bytebuffer.h"
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

// Keep the last RESTORE_BUFFER bytes of resolved data in memory, so that we
// don't have to seek on the disk while building above mentioned tree. Todo,
// this was benchmarked in 2010, test if still valid today
const size_t RESTORE_BUFFER = 256 * M;

const size_t IDENTICAL_FILE_SIZE = 4 * 4096;

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
bool diff_flag = false;
bool compress_flag = false;
bool list_flag = false;
bool named_pipes = false;
bool follow_symlinks = false;
bool shadow_copy = false;
bool absolute_path = false;
bool hash_flag = false;
bool build_info_flag = false;
bool statistics_flag = false;
bool no_timestamp_flag = false;

uint32_t verbose_level = 1;
uint32_t megabyte_flag = 0;
uint32_t gigabyte_flag = 0;
uint32_t threads_flag = 0;
uint32_t compression_level = 1;

// statistics to show to user
uint64_t files = 0;
uint64_t dirs = 0;

uint64_t unchanged = 0; // payload of unchanged files between a full and diff backup
uint64_t identical = 0;
uint64_t identical_files_count = 0;
uint64_t high_entropy_files;
uint64_t unchanged_files = 0;
uint64_t contents_size = 0;
uint64_t hash_salt;

STRING full;
STRING diff;
STRING directory;
vector<STRING> inputfiles;
STRING name;
vector<STRING> restorelist; // optional list of individual files/dirs to restore
vector<STRING> excludelist;
STRING lua = L("");
vector<STRING> shadows;

vector<STRING> entropy_ext;

FILE *ofile = 0;
FILE *ifile = 0;

Cio io = Cio();
Statusbar statusbar;

char *in, *out;
uint64_t bits;

// various
vector<STRING> argv;
int argc;
STRING flags;
STRING output_file;
void *hashtable;
uint64_t file_id_counter = 0;

std::vector<char> restore_buffer_in;
std::vector<char> restore_buffer_out;

Bytebuffer bytebuffer(RESTORE_BUFFER);

FileTypes file_types;
IdenticalFiles identical_files;
UntouchedFiles untouched_files2;

struct file_offset_t {
    STRING filename;
    uint64_t offset = 0;
    FILE *handle = nullptr;
};

vector<file_offset_t> infiles;
vector<contents_t> contents;

std::mutex abort_mutex; // todo, new error handling in exdupe 4

// only used when restoring from file, not when restoring from stdin
struct packet_t {
    bool is_reference = false;
    uint64_t payload = 0;
    size_t length = 0;
    
    // if is_reference, then this points into the stream of restored data.
    std::optional<uint64_t> payload_reference;

    // if !is_reference, then this points at a literal packet into the full or diff file
    std::optional<uint64_t> archive_offset;
};
vector<packet_t> packets;

#ifdef _WIN32
void abort(bool b, int ret, const std::wstring &s) {
    if (b) {
        abort_mutex.lock();
        statusbar.print_abort_message(L("%s"), s.c_str());
        CERR << std::endl << s << std::endl;
        cleanup_and_exit(ret); // todo, kill threads first
    }
}
#endif

void abort(bool b, int ret, const std::string &s) {
    if (b) {
        abort_mutex.lock();
        STRING w = STRING(s.begin(), s.end());
        statusbar.print_abort_message(L("%s"), w.c_str());
        cleanup_and_exit(ret); // todo, kill threads first
    }
}

// todo, legacy
void abort(bool b, const CHR *fmt, ...) {
    if (b) {
        abort_mutex.lock();
        vector<CHR> buf;
        buf.resize(1 * M);
        va_list argv;
        va_start(argv, fmt);
        VSPRINTF(buf.data(), fmt, argv);
        va_end(argv);
        STRING s(buf.data());
        statusbar.print_abort_message(L("%s"), s.c_str());
        cleanup_and_exit(err_other); // todo, kill threads first
    }
}

uint64_t backup_set_size() {
    // unchanged and identical are not sent to libexdupe
    return dup_counter_payload() + unchanged + identical;
}

// todo, move
void read_hash(FILE* f, contents_t& c) {
    auto len = io.read_compact<uint64_t>(f);
    c.hash = io.read_bin_string(len, f); // todo, make std::string reading function

    if (len > 0) {
        c.first = io.read_ui<uint64_t>(f);
        c.last = io.read_ui<uint64_t>(f);
    }
}

void write_hash(FILE* f, const contents_t& c) {
    io.write_compact<uint64_t>(c.hash.size(), f);
    io.write(c.hash.data(), c.hash.size(), f);

    if (c.hash.size() > 0) {
        io.write_ui<uint64_t>(c.first, f);
        io.write_ui<uint64_t>(c.last, f);
    }
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

void update_statusbar_backup(STRING file, bool message = false) {
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
    SPRINTF(dst, L("%04u-%02u-%02u %02u:%02u"), date2.tm_year + 1900, date2.tm_mon + 1, date2.tm_mday, date2.tm_hour, date2.tm_min);
    return STRING(dst);
}

STRING validchars(STRING filename) {
#ifdef _WIN32
    std::wregex invalid(L"[<>:\"/\\|?*]");
    return std::regex_replace(filename, invalid, L"=");
#else
    return filename;
#endif
}


void read_content_item(FILE* file, contents_t& c) {
    uint8_t type = io.read_ui<uint8_t>(file);
    c.directory = ((type >> 0) & 1) == 1;
    c.symlink = ((type >> 1) & 1) == 1;
    c.unchanged = ((type >> 2) & 1) == 1;
    c.is_duplicate_of_full = ((type >> 3) & 1) == 1;
    c.is_duplicate_of_diff = ((type >> 4) & 1) == 1;
    c.in_diff = ((type >> 5) & 1) == 1;
    c.windows = ((type >> 6) & 1) == 1;

    c.file_id = io.read_compact<uint64_t>(file);
    if (c.unchanged) {
        return;
    }
    c.abs_path = slashify(io.read_utf8_string(file));
    c.payload = io.read_compact<uint64_t>(file);
    c.name = slashify(io.read_utf8_string(file));
    c.link = slashify(io.read_utf8_string(file));
    c.size = io.read_compact<uint64_t>(file);
    c.checksum = io.read_ui<uint32_t>(file);
    c.file_c_time = io.read_compact<uint64_t>(file);
    c.file_modified = io.read_compact<uint64_t>(file);
    c.attributes = io.read_ui<uint32_t>(file);
    c.duplicate = io.read_compact<uint64_t>(file);

    read_hash(file, c);

    if (!c.directory) {
        STRING i = c.name;
        c.name = slashify(validchars(c.name));
        if (i != c.name) {
            statusbar.print(2, L("*nix filename '%s' renamed to '%s'"), i.c_str(), c.name.c_str());
        }
    }
}

void write_contents_item(FILE *file, const contents_t &c) {
    uint64_t written = io.write_count;
    uint8_t type = ((c.directory ? 1 : 0) << 0) 
        | ((c.symlink ? 1 : 0) << 1) 
        | ((c.unchanged ? 1 : 0) << 2)
        | ((c.is_duplicate_of_full ? 1 : 0) << 3) 
        | ((c.is_duplicate_of_diff ? 1 : 0) << 4) 
        | ((c.in_diff ? 1 : 0) << 5)
        | ((c.windows ? 1 : 0) << 6);

    io.write_ui<uint8_t>(type, file);
    io.write_compact<uint64_t>(c.file_id, file);

    if(!c.unchanged) {
        io.write_utf8_string(c.abs_path, file);
        io.write_compact<uint64_t>(c.payload, file);
        io.write_utf8_string(c.name, file);
        io.write_utf8_string(c.link, file);
        io.write_compact<uint64_t>(c.size, file);
        io.write_ui<uint32_t>(c.checksum, file);
        io.write_compact<uint64_t>(c.file_c_time, file);
        io.write_compact<uint64_t>(c.file_modified, file);
        io.write_ui<uint32_t>(c.attributes, file);
        io.write_compact<uint64_t>(c.duplicate, file);
        write_hash(file, c);
    }
    contents_size += io.write_count - written;
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

void add_packets(const char *src, size_t len, uint64_t archive_offset) {
    size_t pos = 0;

    while (pos < len) {
        packet_t ref;
        uint64_t payload;
        size_t len2;

        int r = dup_packet_info(src + pos, &len2, &payload);
        rassert(r == DUP_REFERENCE || r == DUP_LITERAL);

        ref.length = len2;
        ref.payload = pay_count;
        pay_count += len2;

        if (r == DUP_REFERENCE) {
            // reference
            ref.is_reference = true;
            ref.payload_reference = payload;
        } else if (r == DUP_LITERAL) {
            // raw data chunk
            ref.is_reference = false;
            ref.archive_offset = archive_offset + pos;
        } 
        packets.push_back(ref);
        pos += dup_size_compressed(src + pos);
    }
}


uint64_t seek_to_header(FILE *file, const string &header) {
    //  archive   HEADER  data  sizeofdata  HEADER  data  sizeofdata
    uint64_t orig = io.tell(file);
    uint64_t s;
    string h = "";
    int i = io.seek(file, -3, SEEK_END);
    abort(i != 0, L("Archive corrupted or on a non-seekable device"));
    abort(io.read_bin_string(3, file) != "END", L("Unexpected end of archive (header end tag)"));
    io.seek(file, -3, SEEK_END);

    while (h != header) {
        abort(io.seek(file, -8, SEEK_CUR) != 0, L("Cannot find header '%s'"), s2w(header).c_str());
        s = io.read_ui<uint64_t>(file);
        abort(io.seek(file, -8 - s - 8, SEEK_CUR) != 0, L("Cannot find header '%s'"), s2w(header).c_str());
        h = io.read_bin_string(8, file);
        abort(io.seek(file, -8, SEEK_CUR) != 0, L("Cannot find header '%s'"), s2w(header).c_str());
    }
    io.seek(file, 8, SEEK_CUR);
    return orig;
}

uint64_t read_packets(FILE *file) {
    uint64_t orig = seek_to_header(file, "REFERENC");
    uint64_t n = io.read_ui<uint64_t>(file);
    uint64_t added_payload = 0;

    for (uint64_t i = 0; i < n; i++) {
        packet_t ref;
        uint8_t r = io.read_ui<uint8_t>(file);
        massert(r == DUP_REFERENCE || r == DUP_LITERAL, "Internal error or archive corrupted", "");
        ref.is_reference = r == DUP_REFERENCE;
        if (ref.is_reference) {
            ref.payload_reference = io.read_ui<uint64_t>(file);
        } else {
            ref.archive_offset = io.read_ui<uint64_t>(file);
        }
        ref.payload = io.read_ui<uint64_t>(file);
        ref.length = io.read_ui<uint32_t>(file);

        added_payload += ref.length;
        packets.push_back(ref);
    }

    io.seek(file, orig, SEEK_SET);
    return added_payload;
}

size_t write_packets(FILE* file) {
    io.write("REFERENC", 8, file);
    uint64_t w = io.write_count;
    io.write_ui<uint64_t>(packets.size(), file);

    for (size_t i = 0; i < packets.size(); i++) {
        io.write_ui<uint8_t>(packets.at(i).is_reference ? DUP_REFERENCE : DUP_LITERAL, file);
        if (packets.at(i).is_reference) {
            io.write_ui<uint64_t>(packets.at(i).payload_reference.value(), file);
        }
        else {
            io.write_ui<uint64_t>(packets.at(i).archive_offset.value(), file);
        }

        io.write_ui<uint64_t>(packets.at(i).payload, file);
        io.write_ui<uint32_t>(static_cast<uint32_t>(packets.at(i).length), file);
    }

    io.write_ui<uint32_t>(0, file);
    io.write_ui<uint64_t>(io.write_count - w, file);

    return io.write_count - w;
}


uint64_t find_packet(uint64_t payload) {
    uint64_t lower = 0;
    uint64_t upper = packets.size() - 1;

    if (packets.size() == 0) {
        return std::numeric_limits<uint64_t>::max();
    }

    while (upper != lower) {
        uint64_t middle = lower + (upper - lower) / 2;

        if (packets.at(middle).payload + packets.at(middle).length - 1 < payload) {
            lower = middle + 1;
        } else {
            upper = middle;
        }
    }

    if (packets.at(lower).payload <= payload && packets.at(lower).payload + packets.at(lower).length - 1 >= payload) {
        return lower;
    } else {
        return std::numeric_limits<uint64_t>::max();
    }
}

bool resolve(uint64_t payload, size_t size, char *dst, FILE *ifile, FILE *fdiff, uint64_t splitpay) {
    size_t bytes_resolved = 0;

    while (bytes_resolved < size) {
        uint64_t rr = find_packet(payload + bytes_resolved);
        rassert(rr != std::numeric_limits<uint64_t>::max());
        packet_t ref = packets.at(rr);
        uint64_t prior = payload + bytes_resolved - ref.payload;
        size_t needed = size - bytes_resolved;
        size_t ref_has = ref.length - prior >= needed ? needed : ref.length - prior;

        if (ref.is_reference) {
            resolve(ref.payload_reference.value() + prior, ref_has, dst + bytes_resolved, ifile, fdiff, splitpay);
        } else {

            char *b = bytebuffer.buffer_find(ref.payload, ref_has);
            if (b != 0) {
                memcpy(dst + bytes_resolved, b + prior, ref_has);
            } else {
                FILE *f;
                if (ref.payload >= splitpay) {
                    f = fdiff;
                } else {
                    f = ifile;
                }

                // seek and read and decompress literal packet
                uint64_t p;
                uint64_t orig = io.tell(f);
                uint64_t ao = ref.archive_offset.value();
                io.seek(f, ao, SEEK_SET);
                io.read_vector(restore_buffer_in, DUP_HEADER_LEN, 0, f, true);
                size_t lenc = dup_size_compressed(restore_buffer_in.data());
                size_t lend = dup_size_decompressed(restore_buffer_in.data());
                ensure_size(restore_buffer_out, lend + M);
                io.read_vector(restore_buffer_in, lenc - DUP_HEADER_LEN, DUP_HEADER_LEN, f, true);
                int r = dup_decompress(restore_buffer_in.data(), restore_buffer_out.data(), &lenc, &p);
                massert(!(r != 0 && r != 1), "Internal error or archive corrupted", r);
                bytebuffer.buffer_add(restore_buffer_out.data(), ref.payload, ref.length);

                io.seek(f, orig, SEEK_SET);
                memcpy(dst + bytes_resolved, restore_buffer_out.data() + prior, ref_has);
            }
        }
        bytes_resolved += ref_has;
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

bool save_directory(STRING base_dir, STRING path, bool write = false) {
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
        c.attributes = get_attributes(full, false);
        c.directory = true;
        c.symlink = false;

        if (absolute_path) {
            c.name = full;
        } else {
            c.name = path;
        }

        c.link = L("");
        c.payload = 0;
        c.checksum = 0;
        auto d = get_date(full);
        c.file_c_time = d.first;
        c.file_modified = d.second;
        contents.push_back(c);

        if (write && !diff_flag) {
            io.write("I", 1, ofile);
            write_contents_item(ofile, c);
        }

        last_full = full;
        first_time = false;

        return true;
    }
    return false;
}

size_t write_hashtable(FILE *file) {
    size_t t = dup_compress_hashtable(memory_begin);
    io.write("HASHTBLE", 8, file);
    io.write_ui<uint64_t>(t, file);
    io.write(memory_begin, t, file);
    io.write_ui<uint64_t>(t + 8, file);
    return t;
}

uint64_t read_hashtable(FILE *file) {
    uint64_t orig = seek_to_header(file, "HASHTBLE");
    uint64_t s = io.read_ui<uint64_t>(file);
    if (verbose_level > 0) {
        statusbar.clear_line();
        statusbar.update(BACKUP, 0, 0, (STRING() + L("Reading hashtable from full backup...\r")).c_str(), false, true);
    }

    io.read(memory_end - s, s, file);
    io.seek(file, orig, SEEK_SET);
    int i = dup_decompress_hashtable(memory_end - s);
    abort(i != 0, L("'%s' is corrupted or not a .full file (hash table)"), slashify(full).c_str());
    return 0;
}

size_t write_contents(FILE *file) {
    io.write("CONTENTS", 8, file);
    uint64_t w = io.write_count;
    io.write_ui<uint64_t>(contents.size(), file);
    for (size_t i = 0; i < contents.size(); i++) {
        write_contents_item(file, contents.at(i));
    }
    io.write_ui<uint32_t>(0, file);
    io.write_ui<uint64_t>(io.write_count - w, file);
    return io.write_count - w;
}

vector<contents_t> read_contents(FILE* f) {
    vector<contents_t> ret;
    contents_t c;
    uint64_t orig = seek_to_header(f, "CONTENTS");
    uint64_t n = io.read_ui<uint64_t>(f);
    for (uint64_t i = 0; i < n; i++) {
        read_content_item(f, c);
        ret.push_back(c);
    }
    io.seek(f, orig, SEEK_SET);
    return ret;
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
        file = wstring(L"\\\\?\\") + file;
    }
#endif
    FILE *f;
    rassert(mode == 'r' || mode == 'w');
    if (file == L("-stdin")) {
        f = stdin;
    } else if (file == L("-stdout")) {
        f = stdout;
    } else {
        f = io.open(file.c_str(), mode);
        abort(!f && abortfail && mode == 'w', L("Error creating file: %s"), slashify(file2).c_str());
        abort(!f && abortfail && mode == 'r', L("Error opening file for reading: %s"), slashify(file2).c_str());
    }

    return f;
}


uint64_t list_contents() {
    FILE* ffile = try_open(full, 'r', true);
    FILE* file = diff_flag ? try_open(diff, 'r', true) : ffile;
    string header = io.read_bin_string(8, file);
    abort(header == "EXDUPE D" && !diff_flag, L("File is a diff backup. Please use -LD <full file> <diff file to list>"));

    init_content_maps(ffile);

    //todo rewrite into using read_content()
    seek_to_header(file, "CONTENTS");
    uint64_t payload = 0, files = 0;

    uint64_t n = io.read_ui<uint64_t>(file);
    for (uint64_t i = 0; i < n; i++) {
        contents_t c;
        read_content_item(file, c); 

        if (c.symlink) {
            print_file(STRING(c.name + L(" -> ") + STRING(c.link)).c_str(), std::numeric_limits<uint64_t>::max(), c.file_modified);
            files++;
        } else if (c.directory && !c.unchanged) { // if unchanged, then all other fields except file_id have arbitrary values
            if (c.name != L(".\\") && c.name != L("./") && c.name != L("")) {
                static STRING last_full = L("");
                static bool first_time = true;

                STRING full = c.name;
                full = remove_delimitor(full);
                STRING full_orig = full;

                if (full != last_full || first_time) {
                    statusbar.print_no_lf(0, L("%s%s\n"), STRING(full_orig != full ? L("*") : L("")).c_str(), full.c_str());
                    last_full = full;
                    first_time = false;
                }
            }
        } else {
            untouched_files2.initialize_if_untouched(c);
            payload += c.size;
            files++;
            print_file(c.name, c.size, c.file_modified);
        }
    }

    statusbar.print_no_lf(0, L("\n%s B in %s files\n"), del(payload).c_str(), del(files).c_str());

    fclose(ffile);
    return 0;
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
    b += STRING(L(", avx detected: ")) + STRING(dup_is_avx_supported() ? L("yes") : L("no"));

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

            abort(!continue_flag && hFind == INVALID_HANDLE_VALUE, L("Source file(s) '%s' not found"), slashify(files.at(i)).c_str());

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
            size_t e = flags.find_first_not_of(L("-wfhuRrxqcDpiLzkatgmv0123456789B"));
            if (e != string::npos) {
                abort(true, L("Unknown flag -%s"), flags.substr(e, 1).c_str());
            }

            string flagsS = w2s(flags);

            // abort if numeric digits are used with a wrong flag
            if (regx(flagsS, "[^mgwtvix0123456789][0-9]+") != "") {
                abort(true, L("Numeric values must be preceded by m, g, t, v, or x"));
            }

            for (auto t : std::vector<pair<bool &, std::string>>{
                     {no_timestamp_flag, "w"},
                     {restore_flag, "R"},
                     {no_recursion_flag, "r"},
                     {force_flag, "f"},
                     {continue_flag, "c"},
                     {diff_flag, "D"},
                     {named_pipes, "p"},
                     {follow_symlinks, "h"},
                     {list_flag, "L"},
                     {absolute_path, "a"},
                     {hash_flag, "z"},
                     {build_info_flag, "B"},
                     {statistics_flag, "k"},
                 }) {
                if (regx(flagsS, t.second) != "") {
                    t.first = true;
                }
            }

            auto set_int_flag = [&](uint32_t &flag_ref, const string letter) {
                if (regx(flagsS, letter) == "") {
                    return false;
                }
                string f = regx(flagsS, letter + "\\d+");
                abort(f == "", L("-%s flag must be an integer"), letter.c_str());
                int i = atoi(f.substr(1).c_str());
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
                abort(compression_level > 3, L("-x flag value must be 0...3"));
            }
        }
    } // end of while

    if (i == 1 || (!restore_flag && !list_flag)) {
        flags = L("");
        compress_flag = true;
    }

    // todo, add s and p verification
    abort(no_timestamp_flag != 0 && !diff_flag, L("-w flag can only be used for differential backup"));
    abort(megabyte_flag != 0 && gigabyte_flag != 0, L("-m flag not compatible with -g"));
    abort(restore_flag && (no_recursion_flag || continue_flag), L("-R flag not compatible with -n or -c"));
    abort(restore_flag && (megabyte_flag != 0 || gigabyte_flag != 0), L("-m and -t flags not applicable to restore (no memory required)"));
    abort(restore_flag && (threads_flag != 0), L("-t flag not supported for restore"));
    abort(diff_flag && compress_flag && (megabyte_flag != 0 || gigabyte_flag != 0), L("-m and -t flags not applicable to differential backup (uses same memory as full)"));
    abort(hash_flag && diff_flag, L("-h flag not applicable to differential backup"));
    abort(hash_flag && !compress_flag, L("-h flag not applicable to restore"));
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
    if (compress_flag && !diff_flag) {
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
    } else if (compress_flag && diff_flag) {
        // exdupe -D -stdin full diff < payload.txt
        for (int i = flags_exist + 1; i < argc - 2; i++) {
            add_item(argv.at(i));
        }

        abort(argc - 1 < flags_exist + 3, L("Missing arguments. "));
        full = argv.at(argc - 2);
        diff = argv.at(argc - 1);
        if (inputfiles.at(0) == L("-stdin")) {
            abort(argc - 1 < flags_exist + 3, L("Missing arguments. "));
            name = argv.at(flags_exist + 1);
        }
        abort(inputfiles.at(0) == L("-stdin") && argc < 4 + flags_exist, L(".full file from -stdin not supported. "));

        abort(full == L("-stdin"), L(".full file from -stdin not supported. "));

        abort(full == L("-stdout") || (inputfiles.at(0) == L("-stdin") && argc < 2 + flags_exist) || (inputfiles.at(0) != L("-stdin") && argc < 3 + flags_exist),
              L("Syntax error in source or destination. "));

        abort(inputfiles.at(0) == L("-stdin") && argc - 1 > flags_exist + 3, L("Too many arguments. "));

    } else if (!compress_flag && !diff_flag && !list_flag) {
        abort(argc - 1 < flags_exist + 2, L("Missing arguments. "));
        full = argv.at(1 + flags_exist);
        directory = argv.at(2 + flags_exist);

        abort(full == L("-stdin") && argc - 1 > flags_exist + 2, L("Too many arguments. "));

        for (int i = 0; i < argc - 3 - flags_exist; i++) {
            restorelist.push_back(argv.at(i + 3 + flags_exist));
        }

        abort(directory == L("-stdout") && full == L("-stdin"), L("Restore with both -stdin and -stdout is not supported. One must be a seekable device. "));
        abort(full == L("-stdout") || directory == L("-stdin") || argc < 3 + flags_exist, L("Syntax error in source or destination. "));
    } else if (!compress_flag && diff_flag && !list_flag) {
        abort(argc - 1 < flags_exist + 3, L("Missing arguments. "));
        full = argv.at(1 + flags_exist);
        diff = argv.at(2 + flags_exist);
        directory = argv.at(3 + flags_exist);

        abort(full == L("-stdin") || diff == L("-stdin"), L("-stdin is not supported for restoring differential backup. "));

        for (int i = 0; i < argc - 4 - flags_exist; i++) {
            restorelist.push_back(argv.at(i + 4 + flags_exist));
        }

        abort(full == L("-stdout") || diff == L("-stdout") || (full == L("-stdin") && diff == L("-stdin")) || (argc < 4 + flags_exist), L("Syntax error in source or destination. "));
    } else if (list_flag) {
        abort(!diff_flag && argv.size() < 3, L("Specify a full file. "));
        abort(!diff_flag && argv.size() > 3, L("Too many arguments. "));
        abort(diff_flag && argv.size() != 4, L("Specify both a full and a diff file. "));
        full = argv.at(1 + flags_exist);
        if(diff_flag) {
            diff = argv.at(2 + flags_exist);
        }
    }

    if (compress_flag && inputfiles.at(0) != STRING(L("-stdin"))) {
        vector<STRING> inputfiles2;

        for (uint32_t i = 0; i < inputfiles.size(); i++) {
            if (abs_path(inputfiles.at(i)) == STRING(L(""))) {
                abort(!continue_flag, L("Aborted, does not exist: %s"), slashify(inputfiles.at(i)).c_str());
                statusbar.print(2, L("Skipped, does not exist: %s"), slashify(inputfiles.at(i)).c_str());
            } else {
                inputfiles2.push_back(abs_path(inputfiles.at(i)));
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
    std::string long_help = R"(eXdupe %v file archiver. GPLv2 or later. Copyright 2010 - 2025

Full backup:
  [flags] <sources | -stdin> <dest file | -stdout>

Restore full backup:
  [flags] -R <full backup file> <dest dir | -stdout> [items]
  [flags] -R -stdin <dest dir>

Differential backup:
  [flags] -D <sources> <full backup file> <dest file | -stdout>
  [flags] -D -stdin <full backup file> <dest file>
  
Restore differential backup:
  [flags] -RD <full backup file> <diff backup file> <dest dir | -stdout> [items]

List contents:
  -L <full backup file to list>
  -LD <full backup file> <diff backup file to list>

Show build info: -B

<sources> is a list of files or paths to backup. [items] is a list of files or
paths to restore, written as printed by the -L flag.

Flags:
    -f Overwrite existing files (default is to abort)
    -c Continue if a source file cannot be read (default is to abort)
    -w Read contents of files during differential backup to determine if they
       have changed (default is to look at timestamps only)
   -t# Use # threads (default = 8)
   -g# Use # GB memory for deduplication (default = 2). Set to 1 GB per 20 GB
       of input data for best result. Use -m# to specify MB instead.
   -x# Use compression level # after deduplication (0, 1 = default, 2, 3). Level
       0 means no compression and lets you apply your own
    -- Prefix items in the <sources> list with "--" to exclude them
    -p Include named pipes
    -h Follow symlinks (default is to store symlink only)
    -a Store absolute and complete paths (default is to identify and remove
       any common parent path of the items passed on the command line).
-s"x:" Use Volume Shadow Copy Service for local drive x: (Windows only)
 -u"s" Filter files using a script, s, written in the Lua language. See more
       with -u? flag.
   -z  Use slower cryptographic hash BLAKE3. Default is xxHash128
  -v#  Verbosity # (0 = quiet, 1 = status bar, 2 = skipped files, 3 = all)
   -k  Show deduplication statistics at the end
 -e"x" Don't apply compression or deduplication to files with the file extension
       x. See more with -e? flag.

Example of backup, differential backups and restore:
  exdupe my_dir backup.full
  exdupe -D my_dir backup.full backup1.diff
  exdupe -D my_dir backup.full backup2.diff
  exdupe -RD backup.full backup2.diff restore_dir

More examples:
  exdupe -t12 -g8 dir1 dir2 backup.full
  exdupe -R backup.full restore_dir dir2%/file.txt
  exdupe file.txt -stdout | exdupe -R -stdin restore_dir)";

    std::string short_help = R"(Full backup:
  [flags] <sources | -stdin> <dest file | -stdout>

Restore full backup:
  [flags] -R <full backup file> <dest dir | -stdout>
  [flags] -R -stdin <dest dir>

Differential backup:
  [flags] -D <sources> <full backup file> <dest file | -stdout>
  [flags] -D -stdin <full backup file> <dest file>
  
Restore differential backup:
  [flags] -RD <full backup file> <diff backup file> <dest dir | -stdout>

Most common flags:
    -f Overwrite existing files (default is to abort)
    -c Continue if a source file cannot be read (default is to abort)
   -g# Use # GB memory for deduplication (default = 2). Set to 1 GB per 20 GB
       input data for best result
   -x# Use compression level # after deduplication (0, 1 = default, 2, 3)
    -? Show complete help)";
 
    auto delim = [](std::string& s) {
        s = std::regex_replace(s, std::regex("%/"), WIN ? "\\" : "/");
        s = std::regex_replace(s, std::regex("%v"), VER);
    };
    delim(short_help);
    delim(long_help);

    statusbar.print(0, show_long ? tostring(long_help).c_str() : tostring(short_help).c_str());

    if (VER_DEV != 0) {
        statusbar.print(0, L("\nUNSTABLE DEVELOPMENT VERSION"));
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
  exdupe -u"return true" . backup.full

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

Helper functions:
  contains({list}, value): Test if the list contains the value

All Lua string functions work in utf-8. If path, name or ext are not valid
utf-8 it will be converted by replacing all bytes outside basic ASCII (a-z, A-Z,
0-9 and common symbols) by '?' and then passed to your script.

String and path comparing is case sensitive, but string.upper() and string.
lower() will only change basic ASCII letters. Any other letters remain
unchanged.

Remember to return true for directories in order to traverse them.

Examples:
  -v0 -u"print('added ' .. path .. ': ' .. size); return true"
  -u"return year >= 2024 or is_dir"
  -u"return size < 1000000 or is_dir"
  -u"return not contains({'tmp', 'temp'}, lower(ext))")del";

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

    STRING curdir_case = CASESENSE(slashify(curdir));
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
        abort(!force_flag, L("Destination file '%s' already exists"), slashify(file).c_str());
        try {
            std::filesystem::remove(file);
        } catch (std::exception &) {
            abort(true, L("Failed to overwrite file: %s"), slashify(file).c_str());
        }
    }
}

FILE *create_file(const STRING &file) {
    force_overwrite(file);
    FILE *ret = try_open(file.c_str(), 'w', true);
    return ret;
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
    set_date(path, c.file_modified);
    abort(ret != 0, L("Error creating symlink: %s -> %s"), path.c_str(), c.link.c_str());
}

void ensure_relative(const STRING &path) {
    STRING s = STRING(L("Archive contains absolute paths. Add a [files] argument. ")) + STRING(diff_flag ? STRING() : STRING());
    abort((path.size() >= 2 && path.substr(0, 2) == L("\\\\")) || path.find_last_of(L(":")) != string::npos, s.c_str());
}

// todo, namespace is a temporary fix to separate things
namespace restore {

void set_meta(STRING item, contents_t c) {
    set_date(item, c.file_modified);
    if (c.windows == WIN) {
        set_attributes(item, c.attributes);
    }
}

void restore_from_file(FILE *ffull, FILE *fdiff) {
    FILE *archive_file;
    bool pipe_out = directory == L("-stdout");
    std::vector<char> restore_buffer(RESTORE_CHUNKSIZE, 'c');

    if (diff_flag) {
        archive_file = fdiff;
    } else {
        archive_file = ffull;
    }

    if (!exists(directory)) {
        create_directories(directory, 0);
    }

    contents_t c;
    uint64_t resolved = 0;
    uint64_t basepay = 0;
    STRING curdir = L("");
    STRING base_dir = abs_path(directory);
    statusbar.m_base_dir = base_dir;

    vector<contents_t> dir_meta;
    vector<contents_t> content;

    for (uint32_t i = 0; i < restorelist.size(); i++) {
        restorelist.at(i) = slashify(restorelist.at(i));
        restorelist.at(i) = remove_delimitor(restorelist.at(i));
        restorelist.at(i) = CASESENSE(restorelist.at(i));
    }

    if (diff_flag) {
        basepay = read_packets(ffull);
        read_packets(fdiff);
    } else {
        basepay = read_packets(ffull);
    }

    content = read_contents(archive_file);
    if(diff_flag) {
        for (auto &c : content) {
            untouched_files2.initialize_if_untouched(c);
        }
    }

    verify_restorelist(restorelist, content); 

    for (uint32_t i = 0; i < content.size(); i++) {
        c = content.at(i);

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
                save_directory(L(""), abs_path(dstdir));
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
                checksum_init(&t);
                STRING outfile = remove_delimitor(abs_path(dstdir)) + DELIM_STR + c.name;
                update_statusbar_restore(outfile);
                ofile = pipe_out ? stdout : create_file(outfile);
                resolved = 0;

                if (diff_flag && !c.unchanged && !c.is_duplicate_of_full ) {
                    c.payload += basepay;
                }

                while (resolved < c.size) {
                    size_t process = minimum(c.size - resolved, RESTORE_CHUNKSIZE);
                    resolve(c.payload + resolved, process, restore_buffer.data(), ffull, fdiff, basepay);
                    checksum(restore_buffer.data(), process, &t);
                    io.write(restore_buffer.data(), process, ofile);
                    update_statusbar_restore(outfile);
                    resolved += process;
                }
                if (!pipe_out) {
                    fclose(ofile);
                    set_meta(dstdir + DELIM_STR + c.name, c);
                }
                abort(c.checksum != t.result32(), err_other, format(L("File checksum error {}"), c.name));
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

void data_block_from_stdin(vector<contents_t> &c) {
    STRING destfile;
    STRING last_file = L("");
    uint64_t payload_orig = payload_written;

    //rassert(false);

    for (;;) {
        size_t len;
        size_t len2;
        uint64_t payload;

        io.read(in, 1, ifile);

        if (in[0] == 'B') {
            return;
        }

        rassert(in[0] == DUP_REFERENCE || in[0] == DUP_LITERAL, "Source file error");

        io.read(in + 1, DUP_HEADER_LEN - 1, ifile);
        len = dup_size_compressed(in);
        io.read(in + DUP_HEADER_LEN, len - DUP_HEADER_LEN, ifile);
        int r = dup_decompress(in, out, &len, &payload);
        rassert(!(r == -1 || r == -2), r);
        rassert(c.size() > 0);
        payload_orig = c.at(0).payload;

        if (r == 0) {
            // dup_decompress() wrote literal at the destination, nothing we need to do
        } else if (r == 1) {
            // dup_decompress() returned a reference into a past written file
            size_t resolved = 0;
            while (resolved < len) {
                if (payload + resolved >= payload_orig) {
                    size_t fo = belongs_to(payload + resolved);
                    int j = io.seek(ofile, payload + resolved - payload_orig, SEEK_SET);
                    massert(j == 0, "Internal error or destination drive is not seekable", infiles.at(fo).filename, payload, payload_orig);
                    len2 = io.read(out + resolved, len - resolved, ofile, false);
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
                    len2 = io.read(out + resolved, len - resolved, ifile2, false);
                    resolved += len2;
                    fclose(ifile2);
                }
            }
        } else {
            massert(false, "Internal error or source file corrupted", r);
        }

        uint64_t src_consumed = 0;

        while (c.size() > 0 && src_consumed < len) {
            if (ofile == 0) {
                ofile = create_file(c.at(0).extra);
                destfile = c.at(0).extra;
                checksum_init(&decompress_checksum);
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
            auto has = minimum(missing, len - src_consumed);
            curfile_written += has;
            if(verbose_level < 3) {
                // level 3 doesn't show progress
                update_statusbar_restore(destfile);
            }
            io.write(out + src_consumed, has, ofile);
            checksum(out + src_consumed, has, &decompress_checksum);

            payload_written += has;
            src_consumed += has;

            if (curfile_written == c.at(0).size) {
                io.close(ofile);
                set_meta(c.at(0).extra, c.at(0));
                ofile = 0;
                curfile_written = 0;
                abort(c.at(0).checksum != decompress_checksum.result32(), err_other, format(L("File checksum error {}"), c.at(0).extra));
                c.erase(c.begin());
            }
        }
    }
}


void restore_from_stdin(const STRING& extract_dir) {
    STRING curdir;
    size_t r = 0;
    STRING base_dir = abs_path(extract_dir);
    statusbar.m_base_dir = base_dir;

    curdir = extract_dir;
    // ensure_relative(curdir);
    save_directory(L(""), curdir + DELIM_STR); // initial root

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
            save_directory(L(""), curdir);
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
                // May not have a corresponding data block ('A' block) to trigger decompress_files()
                FILE* h = create_file(buf2);
                files++;
                io.close(h);
                set_meta(buf2, c);
            }
            else {
                c.extra = buf2;
                c.checksum = 0;
                file_queue.push_back(c);

                written.insert({ c.file_id, c.extra });

                update_statusbar_restore(buf2);
                name = c.name;
            }
        }
        else if (w == 'A') {
            data_block_from_stdin(file_queue);
        }
        else if (w == 'C') { // crc
            uint32_t crc = io.read_ui<uint32_t>(ifile);
            file_queue.at(file_queue.size() - 1).checksum = crc;
        }
        else if (w == 'L') { // symlink
            contents_t c;
            files++;
            read_content_item(ifile, c);
            STRING buf2 = curdir + DELIM_CHAR + c.name;
            create_symlink(buf2, c);
        }

        else if (w == 'X') {
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

void compress_symlink(const STRING &link, const STRING &target) {
    bool is_dir;
    STRING tmp;

    time_ms_t file_modified = get_date(link).second;
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

    contents_t c;
    c.unchanged = false;
    c.directory = is_dir;
    c.symlink = true;
    c.link = STRING(tmp);
    c.name = target;
    c.size = 0;
    c.payload = 0;
    c.checksum = 0;
    c.file_modified = file_modified;
    write_contents_item(ofile, c);
    contents.push_back(c);
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

vector<char> dummy(DISK_READ_CHUNK);

void empty_q(bool flush, bool entropy) {
    uint64_t pay;
    size_t cc;
    char* out_result;

    auto write_result = [&]() {
        if (cc > 0) {
            io.write("A", 1, ofile);
            add_packets(out_result, cc, io.write_count);
            io.write(out_result, cc, ofile);
            io.write("B", 1, ofile);
        }
        payload_compressed += pay;
    };

    if (payload_queue_size[current_queue] > 0) {
        cc = dup_compress(payload_queue[current_queue].data(), out_payload_queue[out_current_queue].data(), payload_queue_size[current_queue], &pay, entropy, out_result);
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

void compress_file(const STRING& input_file, const STRING& filename, int attributes) {
    update_statusbar_backupv3(input_file);

    if (input_file != L("-stdin") && ISNAMEDPIPE(attributes) && !named_pipes) {
        auto _ = std::lock_guard(compress_file_mutex);
        statusbar.print(2, L("Skipped, no -p flag for named pipes: %s"), input_file.c_str());
        return;
    }

    pair<time_ms_t, time_ms_t> file_time = input_file == L("-stdin") ? pair<time_ms_t, time_ms_t>(cur_date(), cur_date()) : get_date(input_file);

    checksum_t file_checksum;
    checksum_init(&file_checksum);
    uint64_t file_size = 0;
    contents_t file_meta;
    uint64_t file_read = 0;   

#if 1 // Detect files that are unchanged between full and diff backup, by comparing created and last-modified timestamps
    if (!no_timestamp_flag && diff_flag && input_file != L("-stdin")) {
        auto c = untouched_files2.exists(input_file, filename, file_time);
        if(c) {
            auto _ = std::lock_guard(compress_file_mutex);
            update_statusbar_backup(input_file);
            c->unchanged = true;
            unchanged += c->size;
            unchanged_files++;
            contents.push_back(*c);
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
    file_meta.file_c_time = file_time.first;
    file_meta.file_modified = file_time.second;
    file_meta.attributes = attributes;
    file_meta.directory = false;
    file_meta.symlink = false;
    file_meta.unchanged = false;
    file_meta.in_diff = diff_flag;

    // compress_file() now synchronized
    auto _ = std::lock_guard(compress_file_mutex);

    file_meta.payload = payload_read;
    file_meta.file_id = file_id_counter++;

    files++;

#if 1 // Detect files with identical payload, both within current bacup set, and between full and diff sets
    if(file_size >= IDENTICAL_FILE_SIZE && input_file != L("-stdin")) {
        auto original = identical;
        contents_t cont = identical_files.identical_to(handle, file_meta, io, [](uint64_t n, STRING file) { identical += n; update_statusbar_backup(file); }, input_file);

        if(!cont.hash.empty()) {
            file_meta.payload = cont.payload;
            file_meta.checksum = cont.checksum;
            file_meta.is_duplicate_of_full = !cont.in_diff;
            file_meta.is_duplicate_of_diff = cont.in_diff;
            file_meta.duplicate = cont.file_id;

            if (!diff_flag) {
                // todo clear abs_path?
                io.write("U", 1, ofile);
                write_contents_item(ofile, file_meta);
            }

            identical_files_count++;
            contents.push_back(file_meta);
            io.close(handle);
            return;            
        }
        else {
            identical = original;
        }
    }
#endif

    checksum_init(&file_meta.ct);

    if(!diff_flag) {
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
        checksum(payload_queue[current_queue].data() + payload_queue_size[current_queue], r, &file_meta.ct);

        if (overflows && input_file == L("-stdin") && r == 0) {
            break;
        }

        payload_queue_size[current_queue] += r;
        file_read += r;
        payload_read += r;

        if (file_read == file_size && file_size > 0) {
            // No CRC block for 0-sized files
            io.write("C", 1, ofile);
            file_meta.checksum = file_meta.ct.result32();
            io.write_ui<uint32_t>(file_meta.ct.result32(), ofile);
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

    file_meta.checksum = file_meta.ct.result32();
    file_meta.hash = file_meta.ct.result();
    identical_files.add(file_meta);

    contents.push_back(file_meta);
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
    HANDLE hFind;
    WIN32_FIND_DATAW data;
    hFind = FindFirstFileW(path.c_str(), &data);
    attrib = data.dwFileAttributes;

    if (hFind != INVALID_HANDLE_VALUE) {
        FindClose(hFind);
    }

#endif

    type = is_symlink(path) ? SYMLINK_TYPE : is_dir(path) ? DIR_TYPE : FILE_TYPE;
    date = get_date(path).second;
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
    // Todo, simplify this function by initially creating three distinct lists
    // for files, dirs and symlinks. Instead of iterating through the same list
    // with each their if-conditions

    using item = pair<STRING, int>; // path, attrib

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
            // avoid including full and diff file when compressing
            if ((output_file == L("-stdout") || ((diff.empty() || (!same_path(sub, diff))) && !same_path(sub, full))) && include(sub, top_level)) {
                if ((!ISDIR(type) && !ISSOCK(type)) && !(ISLINK(type) && !follow_symlinks)) {
                    files.emplace_back(item, type);
                }
                else if(ISLINK(type) && !follow_symlinks) {
                    symlinks.emplace_back(item, type);
                }
                else if(ISDIR(type) && (!no_recursion_flag || top_level)) {
                    directories.emplace_back(item, type);
                }
            }
        }
    }


    // First process files
    std::atomic<size_t> ctr = 0;
    const int max_threads = 1;
    std::thread threads[max_threads];

    auto compress_file_function = [&]() {
        size_t j = ctr.fetch_add(1);
        while (j < files.size()) {
            rassert(j < files.size());
            STRING sub = base_dir + files.at(j).first;
            STRING L = files.at(j).first;
            STRING s = right(L) == L("") ? L : right(L);
            compression::compress_file(sub, s, files.at(j).second);            
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


    // then process symlinks (if followed, they will be contained in the files list above instead of here)
    if (!follow_symlinks) {
        for (auto& symlink : symlinks) {
            STRING sub = base_dir + symlink.first;
            save_directory(base_dir, left(symlink.first) + (left(symlink.first) == L("") ? L("") : DELIM_STR), true);
            compress_symlink(sub, right(symlink.first) == L("") ? symlink.first : right(symlink.first));
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
            }

            closedir(dir2);
#endif
            if (dir.first != L("")) {
                dirs++;
            }
            save_directory(base_dir, dir.first, true);
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


void write_header(FILE *file, status_t s, uint64_t mem, bool hash_flag, uint64_t hash_salt, uint64_t archive_id) {
    if (s == BACKUP) {
        io.write("EXDUPE F", 8, file);
    } else if (s == DIFF_BACKUP) {
        io.write("EXDUPE D", 8, file);
    } else {
        rassert(false, s);
    }

    io.write_ui<uint8_t>(VER_MAJOR, file);
    io.write_ui<uint8_t>(VER_MINOR, file);
    io.write_ui<uint8_t>(VER_REVISION, file);
    io.write_ui<uint8_t>(VER_DEV, file);

    io.write_ui<uint64_t>(archive_id, file);

    io.write_ui<uint64_t>(DEDUPE_SMALL, file);
    io.write_ui<uint64_t>(DEDUPE_LARGE, file);

    io.write_ui<uint8_t>(hash_flag ? 1 : 0, file);
    io.write_ui<uint64_t>(hash_salt, file);

    io.write_ui<uint64_t>(mem, file);
}

uint64_t read_header(FILE *file, STRING filename, status_t action, uint64_t* archive_id = nullptr) {
    string header = io.read_bin_string(8, file);
    if (action == BACKUP) {
        abort(header != "EXDUPE F", L("'%s' is not a .full file"), filename.c_str());
    } else if (action == DIFF_BACKUP) {
        abort(header != "EXDUPE D", L("'%s' is not a .diff file"), filename.c_str());
    } else {
        // todo, use class enum
        rassert(false, action);
    }

    int major = io.read_ui<uint8_t>(file);
    int minor = io.read_ui<uint8_t>(file);
    int revision = io.read_ui<uint8_t>(file);
    int dev = io.read_ui<uint8_t>(file);
    (void)dev;

    uint64_t id = io.read_ui<uint64_t>(file);
    if(archive_id) {
        *archive_id = id;
    }

    DEDUPE_SMALL = io.read_ui<uint64_t>(file);
    DEDUPE_LARGE = io.read_ui<uint64_t>(file);

    abort(major != 3, err_other, format("This file was created with eXdupe version {}.{}.{}. Please use {}.x.x on it", major, minor, revision, major));
    abort(dev != VER_DEV, err_other, format("This file was created with eXdupe version {}.{}.{}.dev-{}. Please use the exact same version on it", major, minor, revision, dev));

    hash_flag = io.read_ui<uint8_t>(file) == 1;
    hash_salt = io.read_ui<uint64_t>(file);

    return io.read_ui<uint64_t>(file); // mem usage
}

void wrote_message(uint64_t bytes, uint64_t files) { statusbar.print(1, L("Wrote %s bytes in %s files"), del(bytes).c_str(), del(files).c_str()); }

void create_shadows(void) {
#ifdef _WIN32
    shadow(shadows);

    vector<pair<STRING, STRING>> snaps; //(STRING mount, STRING shadow)
    snaps = get_snaps();
    for (uint32_t i = 0; i < snaps.size(); i++) {
        statusbar.print(3, L("Created snapshot %s -> %s"), snaps.at(i).first.c_str(), snaps.at(i).second.c_str());
    }
#endif
}

#ifdef _WIN32
int wmain(int argc2, CHR *argv2[])
#else
int main(int argc2, char *argv2[])
#endif
{


    tidy_args(argc2, argv2);

    if (argc2 == 1) {
        print_usage(false);
        return 2;
    }
    if (argc2 == 2 && argv.at(1) == L("-u?")) {
        print_lua_help();
        return 0;
    }
    if (argc2 == 2 && argv.at(1) == L("-e?")) {
        print_e_help();
        return 0;
    }

    if (argc2 == 2 && argv.at(1) == L("-?")) {
        print_usage(true);
        return 0;
    }

    parse_flags();

    if (build_info_flag) {
        print_build_info();
        return 0;
    }

    create_shadows();
    parse_files(); // sets "directory"

    file_types.add(entropy_ext);

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_U16TEXT);
#endif
    statusbar.m_verbose_level = verbose_level;

    if (restore_flag || compress_flag || list_flag) {
        // todo, remove these which are now for decompression only. todo, create constants for mem usage
        in = static_cast<char*>(tmalloc(DISK_READ_CHUNK + M));
        out = static_cast<char*>(tmalloc((threads + 1) * DISK_READ_CHUNK + M));
        abort(!in || !out, L("Out of memory"));
        for (uint32_t i = 0; i < threads + 1; i++) {
            compression::payload_queue.push_back(std::vector<char>(DISK_READ_CHUNK + M));
            compression::payload_queue_size.push_back(0);
            compression::out_payload_queue.push_back(std::vector<char>(DISK_READ_CHUNK + M));
            compression::out_payload_queue_size.push_back(0);
        }
    }

    if (list_flag) {
        FILE *ffull = try_open(full, 'r', true);
        read_header(ffull, full, BACKUP); // tests that file version is compatible
        io.close(ffull);
        list_contents();
    } else if (restore_flag && full != L("-stdin") && diff != L("-stdin")) {
        // Restore from file.
        // =================================================================================================
        if (diff_flag) {
            FILE *fdiff = try_open(diff, 'r', true);
            FILE *ffull = try_open(full, 'r', true);
            uint64_t full_id;
            uint64_t diff_id;
            read_header(ffull, full, BACKUP, &full_id);
            read_header(fdiff, diff, DIFF_BACKUP, &diff_id);
            abort(full_id != diff_id, L("The diff file does not belong to the full file. "));
            init_content_maps(ffull);
            restore::restore_from_file(ffull, fdiff);
        } else {
            ifile = try_open(full, 'r', true);
            read_header(ifile, full, BACKUP);
            restore::restore_from_file(ifile, ifile);
        }
        wrote_message(io.write_count, files);
    } else if ((restore_flag && (full == L("-stdin"))) && restorelist.size() == 0) {
        // Restore from stdin. Only entire archive can be restored this way
        STRING s = remove_delimitor(directory);
        ifile = try_open(full, 'r', true);
        read_header(ifile, full, BACKUP);
        restore::restore_from_stdin(s);
        rassert(!diff_flag);
        wrote_message(io.write_count, files);

        // read remainder of file like content section, etc, to avoid error from OS
        vector<std::byte> tmp(32 * 1024, {});
        while (ifile == stdin && io.read(tmp.data(), 32 * 1024, stdin, false)) {
        }
    }

    // Compress
    // =================================================================================================
    else if (compress_flag) {
        uint64_t archive_id;
        if (diff_flag) {
            output_file = diff;
            ofile = create_file(output_file);
            ifile = try_open(full, 'r', true);
            memory_usage = read_header(ifile, full, BACKUP, &archive_id); // also inits hash_salt
            hashtable = malloc(memory_usage);
            abort(!hashtable, err_resources, format("Out of memory. This differential backup requires {} MB. Try -t1 flag", memory_usage >> 20));
            memset(hashtable, 0, memory_usage);
            pay_count = read_packets(ifile); // read size in bytes of user payload in .full file
            packets.clear();

            int r = dup_init(DEDUPE_LARGE, DEDUPE_SMALL, memory_usage, threads, hashtable, compression_level, hash_flag, hash_salt, pay_count);
            abort(r == 1, err_resources, format("Out of memory. This differential backup requires {} MB. Try -t1 flag", memory_usage >> 20));
            abort(r == 2, err_resources, format("Error creating threads. This differential backup requires {} MB memory. Try -t1 flag", memory_usage >> 20));

            read_hashtable(ifile);

            auto con = read_contents(ifile);
            for(auto c : con) {
                c.abs_path = CASESENSE(c.abs_path);
                untouched_files2.add_during_backup(c);
                identical_files.add(c);
            }

            io.close(ifile);

        } else {
            archive_id = rnd64();
            output_file = full;
            ofile = create_file(output_file);
            hash_salt = hash_flag ? rnd64() : 0;
            hashtable = tmalloc(memory_usage);
            abort(!hashtable, err_resources, "Out of memory. Reduce -m, -g or -t flag");
            int r = dup_init(DEDUPE_LARGE, DEDUPE_SMALL, memory_usage, threads, hashtable, compression_level, hash_flag, hash_salt, 0);
            abort(r == 1, err_resources, "Out of memory. Reduce -m, -g or -t flag");
            abort(r == 2, err_resources, "Error creating threads. Reduce -m, -g or -t flag");
        }

        write_header(ofile, diff_flag ? DIFF_BACKUP : BACKUP, memory_usage, hash_flag, hash_salt, archive_id);

        start_time_without_overhead = GetTickCount64();

        if (inputfiles.size() > 0 && inputfiles.at(0) != L("-stdin")) {
            compress_args(inputfiles);
        } else if (inputfiles.size() > 0 && inputfiles.at(0) == L("-stdin")) {
            name = L("stdin");
            compression::compress_file(L("-stdin"), name, 0);
            compression::compress_file_finalize();
        }

        uint64_t end_time_without_overhead = GetTickCount64();

        if (files + dirs == 0) {
            if (output_file != L("-stdout")) {
                fclose(ofile);
                std::filesystem::remove(output_file);
            }
            if (no_recursion_flag) {
                // todo, delete, wildcard no longer needed
                abort(true, err_nofiles, "0 source files or directories. Missing '*' wildcard with -r flag?");
            } else {
                abort(true, err_nofiles, "0 source files or directories");
            }
            // fixme delete the partial (invalid) destination file created
        }

        io.write("X", 1, ofile);
        write_contents(ofile);
        size_t hashtable_size = 0;

        if (!diff_flag) {
            hashtable_size = write_hashtable(ofile);
        }

        size_t references_size = write_packets(ofile);
        io.write("END", 3, ofile);
        if(verbose_level > 0 && verbose_level < 3) {
            statusbar.clear_line();
        }


        if (statistics_flag) {
            uint64_t end_time = GetTickCount64();
            std::ostringstream s;
            int sratio = int((double(io.write_count) / double(backup_set_size() + 1)) * 100.);
            sratio = sratio > 999 ? 999 : sratio == 0 ? 1 : sratio;

            s << "Input:                       " << w2s(del(backup_set_size())) << " B in " << w2s(del(files)) << " files\n";
            s << "Output:                      " << w2s(del(io.write_count)) << " B (" << sratio << "%)\n";
            s << "Speed:                       " << w2s(del(backup_set_size() / ((end_time - start_time) + 1) * 1000  / 1024 / 1024  )) << " MB/s\n";
            s << "Speed w/o init overhead:     " << w2s(del(backup_set_size() / ((end_time_without_overhead - start_time_without_overhead ) + 1) * 1000 / 1024 / 1024)) << " MB/s\n";

            if(diff_flag) {
                s << "Stored as untouched files:   " << suffix(unchanged) << "B in " << w2s(del(unchanged_files)) << " files\n";
            }
            s << "Stored as duplicated files:  " << suffix(identical) << "B in " << w2s(del(identical_files_count)) << " files\n";
            s << "Stored as duplicated blocks: " << suffix(largehits + smallhits) << "B (" << suffix(largehits) << "B large, " << suffix(smallhits) << "B small)\n";
            s << "Stored as literals:          " << suffix(stored_as_literals) << "B (" << suffix(literals_compressed_size) << "B compressed)\n";
            uint64_t total = literals_compressed_size + contents_size + references_size + hashtable_size;
            s << "Overheads:                   " << suffix(contents_size) << "B meta, " << suffix(references_size) << "B refs, " << suffix(hashtable_size) << "B hashtable, " << suffix(io.write_count - total) << "B misc\n";    
            s << "Unhashed due to congestion:  " << suffix(congested_large) << "B large, " << suffix(congested_small) << "B small\n";
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
        else {
            statusbar.print_no_lf(1, L("Compressed %s B in %s files into %sB\n"), del(backup_set_size()).c_str(), del(files).c_str(), s2w(suffix(io.write_count)).c_str());
        }

        io.close(ofile);
    } else {
        print_usage(false);
    }
    return 0;
}
