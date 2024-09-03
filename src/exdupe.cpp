// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#define VER_MAJOR 1
#define VER_MINOR 1
#define VER_REVISION 0
#define VER_DEV 36

#define Q(x) #x
#define QUOTE(x) Q(x)

#define VER QUOTE(VER_MAJOR) "." QUOTE(VER_MINOR) "." QUOTE(VER_REVISION) ".dev" QUOTE(VER_DEV)

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

#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64)
#define WINDOWS
#endif

#ifdef WINDOWS
const bool WIN = true;
#include "shadow/shadow.h"
#include <fcntl.h>
#include <io.h>
#include <windows.h>

#define CURDIR UNITXT(".\\")
#define DELIM_STR UNITXT("\\")
#define DELIM_CHAR UNITXT('\\')
#define LONGLONG UNITXT("%I64d")
#else
#define unsnap(x) x
#define _isatty isatty
#define _fileno fileno

const bool WIN = false;

#include "dirent.h"
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#define CURDIR UNITXT("./")
#define DELIM_STR UNITXT("/")
#define DELIM_CHAR '/'
#define LONGLONG UNITXT("%lld")
#endif

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <set>
#include <map>

#include "bytebuffer.h"
#include "io.hpp"
#include "libexdupe/libexdupe.h"
#include "luawrapper.h"
#include "timestamp.h"
#include "trex/trex.h"
#include "ui.hpp"
#include "unicode.h"
#include "utilities.hpp"

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
const size_t RESTORE_CHUNKSIZE = 128 * K;

// Keep the last RESTORE_BUFFER bytes of resolved data in memory, so that we
// don't have to seek on the disk while building above mentioned tree. Todo,
// this was benchmarked in 2010, test if still valid today
const size_t RESTORE_BUFFER = 64 * M;

#define compile_assert(x) extern int __dummy[(int)x];

compile_assert(sizeof(size_t) == 8);

uint64_t start_time = GetTickCount();

using namespace std;

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
uint64_t tot_res = 0;
uint64_t unchanged = 0;
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
STRING lua = UNITXT("");
vector<STRING> shadows;

FILE *ofile = 0, *ifile = 0;

Cio io = Cio();
Statusbar statusbar;

unsigned char *in, *out;
uint64_t bits;

// various
vector<STRING> argv;
int argc;
STRING flags;
STRING output_file;
bool output_file_mine = false;
void *hashtable;
uint32_t file_id_counter = 0;

Bytebuffer bytebuffer(RESTORE_BUFFER);

STRING tempdiff = UNITXT("EXDUPE.TMP");

typedef struct {
    STRING filename;
    uint64_t offset;
    FILE *handle;
} file_offset_t;

vector<file_offset_t> infiles;

class contents_t {
public:
    bool unchanged = false;
    STRING name;
    STRING link;
    uint64_t size;
    uint64_t payload;
    uint32_t checksum;
    time_t file_modified;
    time_t file_c_time; // created on Windows, status change on nix
    int attributes;
    bool directory;
    bool symlink;
    checksum_t ct;
    STRING extra;
    STRING abs_path;
    uint32_t file_id; // diff files refer to this for unchanged files
};

vector<contents_t> contents;

// Only used during diff backup/restore
unordered_map<STRING, contents_t> contents_full; // {abs_path, contents}
unordered_map<uint32_t, STRING> contents_full_ids; // {file_id, abs_path}

typedef struct {
    uint64_t payload;
    uint64_t archive_offset;
    uint64_t payload_reference;
    size_t length;
    char is_reference;
} reference_t;

vector<reference_t> references;
vector<reference_t> read_ahead;

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

void abort(bool b, const CHR *fmt, ...) {
    if (b) {
        va_list argv;
        va_start(argv, fmt);
        statusbar.clear_line();
        VFPRINTF(stderr, fmt, argv);
        va_end(argv);
        FPRINTF(stderr, UNITXT("\n"));
#ifdef WINDOWS
        unshadow();
#endif
        exit(1);
    }
}

void update_statusbar_backup(STRING file) {
    statusbar.update(BACKUP, dup_counter_payload() + unchanged, io.write_count, file);
}

void update_statusbar_restore(STRING file) {
    statusbar.update(RESTORE, 0, tot_res, file);
}

STRING date2str(time_t date) {
    if (date == 0) {
        return UNITXT("                ");
    }

    CHR dst[1000];
    tm date2 = local_time_tm(date);
    SPRINTF(dst, UNITXT("%04u-%02u-%02u %02u:%02u"), date2.tm_year + 1900, date2.tm_mon + 1, date2.tm_mday, date2.tm_hour, date2.tm_min);
    return STRING(dst);
}

void write_contents_item(FILE *file, contents_t *c) {
    uint64_t written = io.write_count;
    io.write_ui<uint8_t>(c->unchanged ? 1 : 0, file);
    io.write_ui<uint32_t>(c->file_id, file);
    if(!c->unchanged) {
        io.writestr(c->abs_path, file);
        io.write_ui<uint64_t>(c->payload, file);
        io.writestr(c->name, file);
        io.writestr(c->link, file);
        io.write_ui<uint64_t>(c->size, file);
        io.write_ui<uint32_t>(c->checksum, file);
        io.write_ui<uint32_t>(static_cast<uint32_t>(c->file_c_time), file);
        io.write_ui<uint32_t>(static_cast<uint32_t>(c->file_modified), file);
        io.write_ui<uint32_t>(c->attributes, file);
        io.write_ui<uint8_t>(c->directory ? 1 : 0, file);
        io.write_ui<uint8_t>(c->symlink ? 1 : 0, file);
    }
    contents_size += io.write_count - written;
}

void add_file(const STRING &file, uint64_t offset) {
    file_offset_t t;
    t.filename = file;
    t.offset = offset;
    t.handle = 0;
    infiles.push_back(t);
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
uint64_t belongs_to(uint64_t offset) // todo: verify that this algorithm also
                                     // works for infiles.size() == 2
{
    // belongs_to requires first element to be 0!
    abort(infiles.size() == 0, UNITXT("infiles.size() == 0"));
    abort(infiles.at(0).offset != 0, UNITXT("infiles[0].offset != 0"));

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

void add_references(const unsigned char *src, size_t len, uint64_t archive_offset) {
    size_t pos = 0;
    reference_t ref;
    uint64_t payload;
    size_t len2 = std::numeric_limits<size_t>::max();

    while (pos < len) {
        int r = dup_decompress_simulate(src + pos, &len2, &payload);

        ref.length = len2;
        ref.is_reference = static_cast<char>(r);
        ref.payload = pay_count;
        pay_count += len2;

        if (r == 0) {
            // raw data chunk
            ref.payload_reference = 0;
            ref.archive_offset = archive_offset + pos;
        } else if (r == 1) {
            // reference
            ref.payload_reference = payload;
            ref.archive_offset = 0;
        } else {
            abort(true, UNITXT("Internal error, dup_decompress_simulate() = %d"), r);
        }
        references.push_back(ref);
        pos += dup_size_compressed(src + pos);
    }
}

size_t write_references(FILE *file) {
    io.try_write("REFERENC", 8, file);
    uint64_t w = io.write_count;
    io.write_ui<uint64_t>(references.size(), file);

    for (size_t i = 0; i < references.size(); i++) {
        io.write_ui<uint8_t>(references.at(i).is_reference, file);
        if (references.at(i).is_reference) {
            io.write_ui<uint64_t>(references.at(i).payload_reference, file);
        } else {
            io.write_ui<uint64_t>(references.at(i).archive_offset, file);
        }

        io.write_ui<uint64_t>(references.at(i).payload, file);
        io.write_ui<uint32_t>(static_cast<uint32_t>(references.at(i).length), file);
    }

    io.write_ui<uint32_t>(0, file);
    io.write_ui<uint64_t>(io.write_count - w, file);

    return io.write_count - w;
}

uint64_t seek_to_header(FILE *file, const string &header) {
    //  archive   HEADER  data  sizeofdata  HEADER  data  sizeofdata
    uint64_t orig = io.tell(file);
    uint64_t s;
    string h = "";
    int i = io.seek(file, -3, SEEK_END);
    abort(i != 0, UNITXT("Archive corrupted or on a non-seekable device"));
    abort(io.try_read(3, file) != "END", UNITXT("Unexpected end of archive (header end tag)"));
    io.seek(file, -3, SEEK_END);

    while (h != header) {
        abort(io.seek(file, -8, SEEK_CUR) != 0, UNITXT("Cannot find header '%s'"), s2w(header).c_str());
        s = io.read_ui<uint64_t>(file);
        abort(io.seek(file, -8 - s - 8, SEEK_CUR) != 0, UNITXT("Cannot find header '%s'"), s2w(header).c_str());
        h = io.try_read(8, file);
        abort(io.seek(file, -8, SEEK_CUR) != 0, UNITXT("Cannot find header '%s'"), s2w(header).c_str());
    }
    io.seek(file, 8, SEEK_CUR);
    return orig;
}

uint64_t read_references(FILE *file) {
    uint64_t orig = seek_to_header(file, "REFERENC");
    uint64_t n = io.read_ui<uint64_t>(file);
    uint64_t added_payload = 0;

    for (uint64_t i = 0; i < n; i++) {
        reference_t ref;

        ref.is_reference = io.read_ui<uint8_t>(file);
        if (ref.is_reference) {
            ref.payload_reference = io.read_ui<uint64_t>(file);
        } else {
            ref.archive_offset = io.read_ui<uint64_t>(file);
        }
        ref.payload = io.read_ui<uint64_t>(file);
        ref.length = io.read_ui<uint32_t>(file);

        added_payload += ref.length;
        references.push_back(ref);
    }

    io.seek(file, orig, SEEK_SET);
    return added_payload;
}

uint64_t find_reference(uint64_t payload) {
    uint64_t lower = 0;
    uint64_t upper = references.size() - 1;

    if (references.size() == 0) {
        return std::numeric_limits<uint64_t>::max();
    }

    while (upper != lower) {
        uint64_t middle = lower + (upper - lower) / 2;

        if (references.at(middle).payload + references.at(middle).length - 1 < payload) {
            lower = middle + 1;
        } else {
            upper = middle;
        }
    }

    if (references.at(lower).payload <= payload && references.at(lower).payload + references.at(lower).length - 1 >= payload) {
        return lower;
    } else {
        return std::numeric_limits<uint64_t>::max();
    }
}

bool resolve(uint64_t payload, size_t size, unsigned char *dst, FILE *ifile, FILE *fdiff, uint64_t splitpay) {
    std::vector<unsigned char> vin;
    std::vector<unsigned char> vout;

    size_t bytes_resolved = 0;

    while (bytes_resolved < size) {
        uint64_t rr = find_reference(payload + bytes_resolved);
        if (rr == std::numeric_limits<uint64_t>::max()) {
            abort(true, UNITXT("Internal error, find_reference() = -1"));
        }
        uint64_t prior = payload + bytes_resolved - references.at(rr).payload;
        size_t needed = size - bytes_resolved;
        size_t ref_has = references.at(rr).length - prior >= needed ? needed : references.at(rr).length - prior;

        if (references.at(rr).is_reference) {
            resolve(references.at(rr).payload_reference + prior, ref_has, dst + bytes_resolved, ifile, fdiff, splitpay);
        } else {

            char *b = bytebuffer.buffer_find(references.at(rr).payload, ref_has);
            if (b != 0) {
                memcpy(dst + bytes_resolved, b + prior, ref_has);
            } else {
                FILE *f;
                if (references.at(rr).payload >= splitpay) {
                    f = fdiff;
                } else {
                    f = ifile;
                }

                // seek and read and decompress literal packet
                uint64_t orig = io.tell(f);
                uint64_t ao = references.at(rr).archive_offset;
                io.seek(f, ao, SEEK_SET);
                vin.resize(DUP_HEADER_LEN, 'c');
                io.try_read_buf(vin.data(), DUP_HEADER_LEN, f);
                size_t lenc = dup_size_compressed(vin.data());
                size_t lend = dup_size_decompressed(vin.data());
                vin.resize(lenc + M - DUP_HEADER_LEN, 'c');
                vout.resize(lend + M, 'c');
                io.try_read_buf(vin.data() + DUP_HEADER_LEN, lenc - DUP_HEADER_LEN, f);
                uint64_t p;
                int r = dup_decompress(vin.data(), vout.data(), &lenc, &p);

                if (r != 0 && r != 1) {
                    abort(true, UNITXT("Internal error, dup_decompress() = %d"), r);
                }

                bytebuffer.buffer_add(vout.data(), references.at(rr).payload, references.at(rr).length);

                io.seek(f, orig, SEEK_SET);
                memcpy(dst + bytes_resolved, vout.data() + prior, ref_has);
            }
        }
        bytes_resolved += ref_has;
    }

    return false;
}
// clang-format off
void print_file(STRING filename, uint64_t size, time_t file_modified = 0, int attributes = 0) {
#ifdef WINDOWS
    statusbar.print_no_lf(0, UNITXT("%s  %s  %s\n"), 
        size == std::numeric_limits<uint64_t>::max() ? UNITXT("                   ") : del(size, 19).c_str(),
/*
        attributes & FILE_ATTRIBUTE_ARCHIVE ? 'A' : ' ', 
        attributes & FILE_ATTRIBUTE_SYSTEM ? 'S' : ' ',
        attributes & FILE_ATTRIBUTE_HIDDEN ? 'H' : ' ',
        attributes & FILE_ATTRIBUTE_READONLY ? 'R' : ' ',
        attributes & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED ? 'I' : ' ',
*/
        date2str(file_modified).c_str(), filename.c_str());
#else
    statusbar.print_no_lf(0, UNITXT("%s  %s  %s\n"), size == std::numeric_limits<uint64_t>::max() ? UNITXT("                   ") : del(size, 19).c_str(), date2str(file_modified).c_str(), filename.c_str());
#endif
}
// clang-format on

bool save_directory(STRING base_dir, STRING path, bool write = false) {
    static STRING last_full = UNITXT("");
    static bool first_time = true;

    STRING full = base_dir + path;
    full = remove_delimitor(full) + DELIM_STR;
    STRING full_orig = full;

#ifdef WINDOWS
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
        c.directory = true;
        c.symlink = false;

        if (absolute_path) {
            c.name = full;
        } else {
            c.name = path;
        }

        c.link = UNITXT("");
        c.payload = 0;
        c.checksum = 0;
        auto d = get_date(full);
        c.file_c_time = d.first;
        c.file_modified = d.second;
        contents.push_back(c);

        if (write && !diff_flag) {
            io.try_write("I", 1, ofile);
            write_contents_item(ofile, &c);
        }

        last_full = full;
        first_time = false;

        return true;
    }
    return false;
}

size_t write_hashtable(FILE *file) {
    size_t t = dup_compress_hashtable();
    io.try_write("HASHTBLE", 8, file);
    io.write_ui<uint64_t>(t, file);
    io.try_write(hashtable, t, file);
    io.write_ui<uint64_t>(t + 8, file);
#ifdef _DEBUG
    dup_decompress_hashtable(t);
#endif
    return t;
}

uint64_t read_hashtable(FILE *file) {
    uint64_t orig = seek_to_header(file, "HASHTBLE");
    uint64_t s = io.read_ui<uint64_t>(file);
    if (verbose_level > 0) {
        statusbar.clear_line();
        statusbar.update(BACKUP, 0, 0, (STRING() + UNITXT("Reading hashtable from full backup...\r")).c_str(), false, true);
    }
    io.try_read_buf(hashtable, s, file);
    io.seek(file, orig, SEEK_SET);
    int i = dup_decompress_hashtable(s);
    abort(i != 0, UNITXT("'%s' is corrupted or not a .full file (hash table)"), slashify(full).c_str());
    return 0;
}

size_t write_contents(FILE *file) {
    io.try_write("CONTENTS", 8, file);
    uint64_t w = io.write_count;
    io.write_ui<uint64_t>(contents.size(), file);
    for (size_t i = 0; i < contents.size(); i++) {
        write_contents_item(file, &contents.at(i));
    }
    io.write_ui<uint32_t>(0, file);
    io.write_ui<uint64_t>(io.write_count - w, file);
    return io.write_count - w;
}

STRING validchars(STRING filename) {
#ifdef WINDOWS
    std::wregex invalid(L"[<>:\"/\\|?*]");
    return std::regex_replace(filename, invalid, L"=");
#else
    return filename;
#endif
}

void read_content_item(FILE *file, contents_t *c) {
    c->unchanged = io.read_ui<uint8_t>(file) == 0 ? false : true;
    c->file_id = io.read_ui<uint32_t>(file);
    if(c->unchanged) {
        return;
    }
    c->abs_path = slashify(io.readstr(file));
    c->payload = io.read_ui<uint64_t>(file);
    c->name = slashify(io.readstr(file));
    c->link = slashify(io.readstr(file));
    c->size = io.read_ui<uint64_t>(file);
    c->checksum = io.read_ui<uint32_t>(file);
    c->file_c_time = io.read_ui<uint32_t>(file);
    c->file_modified = io.read_ui<uint32_t>(file);
    c->attributes = io.read_ui<uint32_t>(file);
    c->directory = io.read_ui<uint8_t>(file) == 0 ? false : true;
    c->symlink = io.read_ui<uint8_t>(file) == 0 ? false : true;
    if (!c->directory) {
        STRING i = c->name;
        c->name = slashify(validchars(c->name));
        if (i != c->name) {
            statusbar.print(2, UNITXT("*nix filename '%s' renamed to '%s'"), i.c_str(), c->name.c_str());
        }
    }
}

vector<contents_t> read_contents(FILE* f) {
    vector<contents_t> ret;
    contents_t c;
    uint64_t orig = seek_to_header(f, "CONTENTS");
    uint64_t n = io.read_ui<uint64_t>(f);
    for (uint64_t i = 0; i < n; i++) {
        read_content_item(f, &c);
        ret.push_back(c);
    }
    io.seek(f, orig, SEEK_SET);
    return ret;
}

void init_content_maps(FILE* ffull) {
    auto con = read_contents(ffull);
    for(auto c : con) {
        if(!c.directory && !c.symlink) {
            abort((contents_full.find(CASESENSE(c.abs_path)) != contents_full.end()), UNITXT("Internal error at main::contents_full.find(), or diff file doesn't belong to full file"));
            contents_full[CASESENSE(c.abs_path)] = c;
            abort((contents_full_ids.find(c.file_id) != contents_full_ids.end()), UNITXT("Internal error at main::contents_full_ids.find(), or diff file doesn't belong to full file"));
            contents_full_ids[c.file_id] = CASESENSE(c.abs_path);
        }
    }
}

FILE *try_open(STRING file2, char mode, bool abortfail) {
    auto file = file2;
#ifdef WINDOWS
    // todo fix properly. A long *relative* path wont work
    if (file.size() > 250) {
        file = wstring(L"\\\\?\\") + file;
    }
#endif
    FILE *f;
    assert(mode == 'r' || mode == 'w');
    if (file == UNITXT("-stdin")) {
        f = stdin;
    } else if (file == UNITXT("-stdout")) {
        f = stdout;
    } else {
        f = io.open(file.c_str(), mode);
        abort(!f && abortfail && mode == 'w', UNITXT("Error creating file: %s"), slashify(file2).c_str());
        abort(!f && abortfail && mode == 'r', UNITXT("Error opening file for reading: %s"), slashify(file2).c_str());
    }

    return f;
}

void resolve_unchanged(contents_t &c) {

            if(c.unchanged) {
                auto id_iter = contents_full_ids.find(c.file_id);
                abort(id_iter == contents_full_ids.end(), UNITXT("Internal error at decompress_individuals::contents_full.find(), or diff file doesn't belong to full file"));
                auto p = id_iter->second;
                auto path_iter = contents_full.find(p);
                abort(path_iter == contents_full.end(), UNITXT("Internal error at decompress_individuals::contents_full.find(), or diff file doesn't belong to full file"));
                // todo, maybe it's better to remove this and let the payload writer access contents_full directly                
                c = path_iter->second;
                c.unchanged = true;
            }
        
}

uint64_t dump_contents() {
    FILE* ffile = try_open(full, 'r', true);
    FILE* file = diff_flag ? try_open(diff, 'r', true) : ffile;
    string header = io.try_read(8, file);
    abort(header == "EXDUPE D" && !diff_flag, UNITXT("File is a diff backup. Please use -LD <full file> <diff file to list>"));

    init_content_maps(ffile);

    //todo rewrite into using read_content()
    seek_to_header(file, "CONTENTS");
    uint64_t payload = 0, files = 0;

    uint64_t n = io.read_ui<uint64_t>(file);
    for (uint64_t i = 0; i < n; i++) {
        contents_t c;
        read_content_item(file, &c); 

        if (c.symlink) {
            print_file(STRING(c.name + UNITXT(" -> ") + STRING(c.link)).c_str(), std::numeric_limits<uint64_t>::max(), c.file_modified, c.attributes);
            files++;
        } else if (c.directory && !c.unchanged) { // if unchanged, then all other fields except file_id have arbitrary values
            if (c.name != UNITXT(".\\") && c.name != UNITXT("./") && c.name != UNITXT("")) {
                static STRING last_full = UNITXT("");
                static bool first_time = true;

                STRING full = c.name;
                full = remove_delimitor(full);
                STRING full_orig = full;

                if (full != last_full || first_time) {
                    statusbar.print_no_lf(0, UNITXT("%s%s\n"), STRING(full_orig != full ? UNITXT("*") : UNITXT("")).c_str(), full.c_str());
                    last_full = full;
                    first_time = false;
                }
            }
        } else {
            resolve_unchanged(c);

            payload += c.size;
            files++;
            print_file(c.name, c.size, c.file_modified, c.attributes);
        }
    }

    statusbar.print_no_lf(0, UNITXT("\n%s B in %s files\n"), del(payload).c_str(), del(files).c_str());

    fclose(ffile);
    return 0;
}

void print_build_info() {
    // "2024-01-04T09:27:05+0100"
    STRING td = UNITXT(_TIMEZ_);
    td = td.substr(0, 10) + UNITXT(" ") + td.substr(11, 8) + UNITXT(" ") + td.substr(19, 5);
    STRING b = STRING(UNITXT("ver " VER ", built ")) + td + UNITXT(", sha ") + UNITXT(GIT_COMMIT_HASH) + UNITXT(", ");
#ifdef NDEBUG
    b += UNITXT("release mode");
#else
    b += UNITXT("debug mode");
#endif
    statusbar.print(0, b.c_str());
}

#ifdef WINDOWS
vector<STRING> wildcard_expand(vector<STRING> files) {
    HANDLE hFind = INVALID_HANDLE_VALUE;
    DWORD dwError;
    WIN32_FIND_DATAW FindFileData;
    vector<STRING> ret;

    for (uint32_t i = 0; i < files.size(); i++) {
        STRING payload_queue = remove_delimitor(files.at(i));
        STRING s = right(payload_queue) == UNITXT("") ? payload_queue : right(payload_queue);
        if (s.find_first_of(UNITXT("*?")) == string::npos) {
            ret.push_back(files.at(i));
        } else {
            vector<STRING> f;
            hFind = FindFirstFileW(files.at(i).c_str(), &FindFileData);

            abort(!continue_flag && hFind == INVALID_HANDLE_VALUE, UNITXT("Source file(s) '%s' not found"), slashify(files.at(i)).c_str());

            if (hFind != INVALID_HANDLE_VALUE) {
                if (STRING(FindFileData.cFileName) != UNITXT(".") && STRING(FindFileData.cFileName) != UNITXT("..")) {
                    f.push_back(FindFileData.cFileName);
                }
                while (FindNextFileW(hFind, &FindFileData) != 0) {
                    if (STRING(FindFileData.cFileName) != UNITXT(".") && STRING(FindFileData.cFileName) != UNITXT("..")) {
                        f.push_back(FindFileData.cFileName);
                    }
                }

                dwError = GetLastError();
                FindClose(hFind);
                abort(dwError != ERROR_NO_MORE_FILES, UNITXT("FindNextFile error. Error is %u"), dwError);

                size_t t = files.at(i).find_last_of(UNITXT("\\/"));
                STRING dir = UNITXT("");
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

    while (argc > i && argv.at(i).substr(0, 1) == UNITXT("-") && argv.at(i).substr(0, 2) != UNITXT("--") && argv.at(i) != UNITXT("-stdin") && argv.at(i) != UNITXT("-stdout")) {
        flags = argv.at(i);
        i++;
        flags_exist++;

        if (flags.length() > 2 && flags.substr(0, 2) == UNITXT("-u")) {
            lua = flags.substr(2);
            abort(lua == UNITXT(""), UNITXT("Missing command in -u flag"));
        } else if (flags.length() > 2 && flags.substr(0, 2) == UNITXT("-s")) {
#ifdef WINDOWS
            STRING mount = flags.substr(2);
            abort(mount == UNITXT(""), UNITXT("Missing drive in -s flag"));
            shadows.push_back(mount);
#else
            abort(true, UNITXT("-s flag not supported in *nix"));
#endif
        } else {
            size_t e = flags.find_first_not_of(UNITXT("-wfhuRrxqcDpiLzkatgmv0123456789B"));
            if (e != string::npos) {
                abort(true, UNITXT("Unknown flag -%s"), flags.substr(e, 1).c_str());
            }

            string flagsS = wstring2string(flags);

            // abort if numeric digits are used with a wrong flag
            if (regx(flagsS, "[^mgwtvix0123456789][0-9]+") != "") {
                abort(true, UNITXT("Numeric values must be preceded by m, g, t, v, or x"));
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
                abort(f == "", UNITXT("-%s flag must be an integer"), letter.c_str());
                int i = atoi(f.substr(1).c_str());
                flag_ref = i;
                return true;
            };

            if (set_int_flag(threads_flag, "t")) {
                if (threads_flag >= 1) {
                    threads = threads_flag;
                } else {
                    abort(true, UNITXT("Invalid -t flag value"));
                }
            }

            if (set_int_flag(gigabyte_flag, "g")) {
                if (gigabyte_flag > 0) {
                    memory_usage = gigabyte_flag * G;
                } else {
                    abort(true, UNITXT("Invalid -g flag value"));
                }
            }

            if (set_int_flag(megabyte_flag, "m")) {
                if (megabyte_flag > 0) {
                    memory_usage = megabyte_flag * M;
                } else {
                    abort(true, UNITXT("Invalid -m flag value"));
                }
            }

            if (set_int_flag(verbose_level, "v")) {
                abort(verbose_level < 0 || verbose_level > 9, UNITXT("-v flag value must be 0...3"));
            }

            if (set_int_flag(compression_level, "x")) {
                abort(compression_level > 3 || compression_level < 0, UNITXT("-x flag value must be 0...3"));
            }
        }
    } // end of while

    if (i == 1 || (!restore_flag && !list_flag)) {
        flags = UNITXT("");
        compress_flag = true;
    }

    // todo, add s and p verification
    abort(no_timestamp_flag != 0 && !diff_flag, UNITXT("-w flag can only be used for differential backup"));
    abort(megabyte_flag != 0 && gigabyte_flag != 0, UNITXT("-m flag not compatible with -g"));
    abort(restore_flag && (no_recursion_flag || continue_flag), UNITXT("-R flag not compatible with -n or -c"));
    abort(restore_flag && (megabyte_flag != 0 || gigabyte_flag != 0), UNITXT("-m and -t flags not applicable to restore (no memory required)"));
    abort(restore_flag && (threads_flag != 0), UNITXT("-t flag not supported for restore"));
    abort(diff_flag && compress_flag && (megabyte_flag != 0 || gigabyte_flag != 0), UNITXT("-m and -t flags not applicable to differential backup (uses same memory as full)"));
    abort(hash_flag && diff_flag, UNITXT("-h flag not applicable to differential backup"));
    abort(hash_flag && !compress_flag, UNITXT("-h flag not applicable to restore"));
}

void add_item(const STRING &item) {
    if (item.size() >= 2 && item.substr(0, 2) == UNITXT("--")) {
        STRING e = item.substr(2);
        e = remove_delimitor(CASESENSE(abs_path(e)));
        if (!(exists(e))) {
            statusbar.print(2, UNITXT("Excluded item '%s' does not exist"), e.c_str());
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

        abort(argc - 1 < flags_exist + 2, UNITXT("Missing arguments. "));
        full = argv.at(argc - 1);
        if (inputfiles.at(0) == UNITXT("-stdin")) {
            abort(argc - 1 < flags_exist + 2, UNITXT("Missing arguments. "));
            name = argv.at(flags_exist + 2);
        }

        abort(inputfiles.at(0) == UNITXT("-stdout") || name == UNITXT("-stdin") || name == UNITXT("-stdout") || full == UNITXT("-stdin") || (inputfiles.at(0) == UNITXT("-stdin") && argc < 3 + flags_exist) || (inputfiles.at(0) != UNITXT("-stdin") && argc < 3 + flags_exist),
              UNITXT("Syntax error in source or destination. "));
    } else if (compress_flag && diff_flag) {
        for (int i = flags_exist + 1; i < argc - 2; i++) {
            add_item(argv.at(i));
        }

        abort(argc - 1 < flags_exist + 3, UNITXT("Missing arguments. "));
        full = argv.at(argc - 2);
        diff = argv.at(argc - 1);
        if (inputfiles.at(0) == UNITXT("-stdin")) {
            abort(argc - 1 < flags_exist + 4, UNITXT("Missing arguments. "));
            name = argv.at(flags_exist + 2);
        }
        abort(inputfiles.at(0) == UNITXT("-stdin") && argc < 5 + flags_exist, UNITXT(".full file from -stdin not supported. "));

        abort(full == UNITXT("-stdin"), UNITXT(".full file from -stdin not supported. "));

        abort(inputfiles.at(0) == UNITXT("-stdout") || name == UNITXT("-stdin") || name == UNITXT("-stdout") || full == UNITXT("-stdout") || (inputfiles.at(0) == UNITXT("-stdin") && argc < 3 + flags_exist) || (inputfiles.at(0) != UNITXT("-stdin") && argc < 3 + flags_exist),
              UNITXT("Syntax error in source or destination. "));
    } else if (!compress_flag && !diff_flag && !list_flag) {
        abort(argc - 1 < flags_exist + 2, UNITXT("Missing arguments. "));
        full = argv.at(1 + flags_exist);
        directory = argv.at(2 + flags_exist);

        abort(full == UNITXT("-stdin") && argc - 1 > flags_exist + 2, UNITXT("Too many arguments. "));

        for (int i = 0; i < argc - 3 - flags_exist; i++) {
            restorelist.push_back(argv.at(i + 3 + flags_exist));
        }

        abort(directory == UNITXT("-stdout") && full == UNITXT("-stdin"), UNITXT("Restore with both -stdin and -stdout is not supported. One must be a seekable device. "));
        abort(full == UNITXT("-stdout") || directory == UNITXT("-stdin") || argc < 3 + flags_exist, UNITXT("Syntax error in source or destination. "));
    } else if (!compress_flag && diff_flag && !list_flag) {
        abort(argc - 1 < flags_exist + 3, UNITXT("Missing arguments. "));
        full = argv.at(1 + flags_exist);
        diff = argv.at(2 + flags_exist);
        directory = argv.at(3 + flags_exist);

        abort(full == UNITXT("-stdin") || diff == UNITXT("-stdin"), UNITXT("-stdin is not supported for restoring differential backup. "));

        for (int i = 0; i < argc - 4 - flags_exist; i++) {
            restorelist.push_back(argv.at(i + 4 + flags_exist));
        }

        //	abort(directory == UNITXT("-stdout"), UNITXT("Restore to stdout
        // or non-seekable drive not supported"));

        abort(full == UNITXT("-stdout") || diff == UNITXT("-stdout") || (full == UNITXT("-stdin") && diff == UNITXT("-stdin")) || (argc < 4 + flags_exist), UNITXT("Syntax error in source or destination. "));
    } else if (list_flag) {
        abort(!diff_flag && argv.size() < 3, UNITXT("Specify a full file. "));
        abort(!diff_flag && argv.size() > 3, UNITXT("Too many arguments. "));
        abort(diff_flag && argv.size() != 4, UNITXT("Specify both a full and a diff file. "));
        full = argv.at(1 + flags_exist);
        if(diff_flag) {
            diff = argv.at(2 + flags_exist);
        }
    }

    if (compress_flag && inputfiles.at(0) != STRING(UNITXT("-stdin"))) {
        vector<STRING> inputfiles2;

        for (uint32_t i = 0; i < inputfiles.size(); i++) {
            if (abs_path(inputfiles.at(i)) == STRING(UNITXT(""))) {
                abort(!continue_flag, UNITXT("Aborted, does not exist: %s"), slashify(inputfiles.at(i)).c_str());
                statusbar.print(2, UNITXT("Skipped, does not exist: %s"), slashify(inputfiles.at(i)).c_str());
            } else {
                inputfiles2.push_back(abs_path(inputfiles.at(i)));
#ifdef WINDOWS
                inputfiles2.back() = snap(inputfiles2.back());
#endif
            }
        }

        inputfiles = inputfiles2;
#ifdef WINDOWS
        inputfiles = wildcard_expand(inputfiles);
#endif
    }
}

STRING tostring(std::string s) { return STRING(s.begin(), s.end()); }

void print_usage(bool show_long) {
    std::string long_help = R"(eXdupe %v file archiver. GPLv2 or later. Copyright 2010 - 2024

Full backup:
  [flags] <sources> <dest file | -stdout>
  [flags] <-stdin> <dest file>

Restore full backup:
  [flags] -R <full backup file> <dest dir | -stdout> [items]
  [flags] -R <-stdin> <dest dir>

Differential backup:
  [flags] -D <sources> <full backup file> <dest file | -stdout>
  [flags] -D <-stdin> <filename to assign> <full backup file> <dest file>
  
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
  -tn Use n threads (default = 8)
  -gn Use n GB memory for deduplication (default = 2). Set to 1 GB per 20 GB
      of input data for best result. Use -mn to specify MB instead.
  -xn Use compression level n after deduplication (0 = none, 1 = zstd-1
      (default), 2 = zstd-10, 3 = zstd-19)
   -- Prefix items in the <sources> list with "--" to exclude them
   -p Include named pipes
   -h Follow symlinks (default is to store symlink only)
   -a Store absolute and complete paths (default is to identify and remove
      any common parent path of the items passed on the command line).
-s"x" Use Volume Shadow Copy Service for local drive x: (Windows only)
-u"s" Filter files using a script, s, written in the Lua language. See more
      with -u? flag.
  -z  Use slower cryptographic hash BLAKE3. Default is xxHash128
 -vn  Verbosity n (0 = quiet, 1 = status bar, 2 = skipped files, 3 = all)
  -k  Show deduplication statistics at the end

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
  [flags] <sources> <dest file | -stdout>
  [flags] <-stdin> <filename to assign> <dest file>

Restore full backup:
  [flags] -R <full backup file> <dest dir | -stdout>
  [flags] -R <-stdin> <dest dir>

Differential backup:
  [flags] -D <sources> <full backup file> <dest file | -stdout>
  [flags] -D <-stdin> <filename to assign> <full backup file> <dest file>
  
Restore differential backup:
  [flags] -RD <full backup file> <diff backup file> <dest dir | -stdout>

Most common flags:
   -f Overwrite existing files (default is to abort)
   -c Continue if a source file cannot be read (default is to abort)
  -gn Use n GB memory for deduplication (default = 2). Set to 1 GB per 20 GB
      input data for best result
  -xn Use compression level n after deduplication (0, 1 = default, 2, 3)
   -? Show complete help)";
 
    for (auto &a : {&long_help, &short_help}) {
        *a = std::regex_replace(*a, std::regex("%/"), WIN ? "\\" : "/");
        *a = std::regex_replace(*a, std::regex("%v"), VER);
    }

    statusbar.print(0, show_long ? tostring(long_help).c_str() : tostring(short_help).c_str());
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
  size:   Size
  attrib: Result of chmod on Linux. On Windows you can reference the booleans
          FILE_ATTRIBUTE_READONLY, FILE_ATTRIBUTE_HIDDEN, etc.
  time:   Last modified time as os.date object. You can also reference these
          integer variables: year, month, day, hour, min, sec

Helper functions:
  contains({list}, value): Test if the list contains the value

All Lua string functions work in utf-8. If path, name or ext are not valid
utf-8 then all bytes outside basic ASCII (a-z, A-Z, 0-9 and common symbols) are
replaced by '?' before being passed to your script.

String and path comparing is case sensitive, but string.upper() and string.
lower() will only change basic ASCII letters. Any other letters remain
unchanged.

Examples:
  -v0 -u"print('added ' .. path .. ': ' .. size); return true"
  -u"return year >= 2024"
  -u"return not contains({'tmp', 'temp'}, lower(ext))")del";

    statusbar.print(0, tostring(lua_help).c_str());
}



STRING parent_path(const vector<STRING> &items) {
    size_t prefix = longest_common_prefix(items, !WIN);
    if (prefix == 0) {
        return UNITXT("");
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

        if (curdir_case == UNITXT("") && curfile == restorelist.at(i)) {
            return make_pair(curdir, i);
        }
    }
    return make_pair(UNITXT(":"), 0);
}

void verify_restorelist(vector<STRING> restorelist, const vector<contents_t> &content) {
    STRING curdir = UNITXT("");
    contents_t c;
    for (uint32_t i = 0; i < content.size(); i++) {
        if (restorelist.size() == 0) {
            break;
        }

        c = content.at(i);
        if (c.directory) {
            if (c.name != UNITXT("./")) { // todo, simplify by not making an archive possible to contain ./ ?
                curdir = remove_delimitor(c.name);
            }
        } else {
            pair<STRING, size_t> p = extract_to(curdir, c.name);
            STRING s = p.first;
        }

        pair<STRING, size_t> p = extract_to(curdir, c.name);
        size_t j = p.second;
        STRING s = p.first;
        if (s != UNITXT(":")) // : means don't extract
        {
            restorelist.at(j) = UNITXT(":");
        }
    }

    for (uint32_t i = 0; i < restorelist.size(); i++) {
        abort(restorelist.at(i) != UNITXT(":"), UNITXT("'%s' does not exist in archive or is included multiple times by your [files] list"), restorelist.at(i).c_str());
    }
}

void force_overwrite(const STRING &file) {
    if (file != UNITXT("-stdout") && exists(file)) {
        abort(!force_flag, UNITXT("Destination file '%s' already exists"), slashify(file).c_str());
        try {
            std::filesystem::remove(file);
        } catch (exception &) {
            abort(true, UNITXT("Failed to overwrite file: %s"), slashify(file).c_str());
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
#ifdef WINDOWS
    int ret = CreateSymbolicLink(path.c_str(), c.link.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE | (c.directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0));
    if (ret == 0) {
        int e = GetLastError();
        abort(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, UNITXT("Plase run eXdupe as administrator to restore symlinks: %s -> %s"), path.c_str(), c.link.c_str());
        abort(true, UNITXT("Unknown error (GetLastError() = %d) restoring symlink: %s -> %s"), e, path.c_str(), c.link.c_str());
    }
    ret = 0;

#else
    int ret = symlink(c.link.c_str(), path.c_str());
#endif
    set_date(path, c.file_modified);
    abort(ret != 0, UNITXT("Error creating symlink: %s -> %s"), path.c_str(), c.link.c_str());
}

void ensure_relative(const STRING &path) {
    STRING s = STRING(UNITXT("Archive contains absolute paths. Add a [files] argument. ")) + STRING(diff_flag ? STRING() : STRING());
    abort((path.size() >= 2 && path.substr(0, 2) == UNITXT("\\\\")) || path.find_last_of(UNITXT(":")) != string::npos, s.c_str());
}


void decompress_individuals(FILE *ffull, FILE *fdiff) {
    FILE *archive_file;
    bool pipe_out = directory == UNITXT("-stdout");
    std::vector<unsigned char> restore_buffer(RESTORE_CHUNKSIZE, 'c');

    if (diff_flag) {
        archive_file = fdiff;
    } else {
        archive_file = ffull;
    }

    if (!exists(directory)) {
        create_directories(directory, 0);
    }

    uint64_t payload = 0;
    contents_t c;
    uint64_t resolved = 0;
    uint64_t basepay = 0;
    STRING curdir = UNITXT("");
    STRING base_dir = abs_path(directory);
    statusbar.m_base_dir = base_dir;

    vector<contents_t> content;

    for (uint32_t i = 0; i < restorelist.size(); i++) {
        restorelist.at(i) = slashify(restorelist.at(i));
        restorelist.at(i) = remove_delimitor(restorelist.at(i));
        restorelist.at(i) = CASESENSE(restorelist.at(i));
    }

    if (diff_flag) {
        basepay = read_references(ffull);
        read_references(fdiff);
    } else {
        basepay = read_references(ffull);
    }

    content = read_contents(archive_file);
    if(diff_flag) {
        for (auto &c : content) {
            resolve_unchanged(c);
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
        if (s != UNITXT(":")) // : means don't extract
        {
            STRING dstdir;
            STRING x = s;

            ensure_relative(x);

            if (x.substr(0, 1) != UNITXT("\\") && x.substr(0, 1) != UNITXT("/")) {
                dstdir = remove_delimitor(base_dir + DELIM_STR + x) + DELIM_STR;
            } else {
                dstdir = remove_delimitor(base_dir + x) + DELIM_STR;
            }

            if (!pipe_out) {
                create_directories(dstdir, c.file_modified);
            }

            if (!pipe_out) {
                save_directory(UNITXT(""), abs_path(dstdir));
            }

            if (c.symlink) {
                files++;
                update_statusbar_restore(c.name + UNITXT(" -> ") + c.link);
                create_symlink(dstdir + c.name, c);
            } else if (!c.directory) {
                files++;
                checksum_t t;
                checksum_init(&t);
                STRING outfile = remove_delimitor(abs_path(dstdir)) + DELIM_STR + c.name;
                update_statusbar_restore(outfile);
                ofile = pipe_out ? stdout : create_file(outfile);
                resolved = 0;

                if (diff_flag && !c.unchanged) {
                    c.payload += basepay;
                }

                while (resolved < c.size) {
                    size_t process = minimum(c.size - resolved, RESTORE_CHUNKSIZE);
                    resolve(c.payload + resolved, process, restore_buffer.data(), ffull, fdiff, basepay);
                    checksum(restore_buffer.data(), process, &t);
                    io.write(restore_buffer.data(), process, ofile);
                    tot_res += process;
                    update_statusbar_restore(outfile);
                    resolved += process;
                    payload += c.size;
                }
                if (!pipe_out) {
                    fclose(ofile);
                    set_date(dstdir + DELIM_STR + c.name, c.file_modified);
                    set_attributes(dstdir + DELIM_STR + c.name, c.attributes);
                }
                abort(c.checksum != t.result, UNITXT("File checksum error"));
            }
        }
    }


}

uint64_t payload_written = 0;
uint64_t add_file_payload = 0;
uint64_t curfile_written = 0;
uint64_t current_outfile_begin = 0;
checksum_t decompress_checksum;

void decompress_files(vector<contents_t> &c, bool add_files) {
    STRING destfile;
    STRING last_file = UNITXT("");
    uint64_t payload_orig = payload_written;

    for (;;) {
        size_t len;
        size_t len2;
        uint64_t payload;

        io.try_read_buf(in, 1, ifile);

        if (*in == 'B') {
            return;
        }

        io.try_read_buf(in + 1, 7, ifile);
        assert((in[0] == 'T' && in[1] == 'T') || (in[0] == 'M' && in[1] == 'M'));

        io.try_read_buf(in + 8, DUP_HEADER_LEN - 8, ifile);
        len = dup_size_compressed(in);
        io.try_read_buf(in + DUP_HEADER_LEN, len - DUP_HEADER_LEN, ifile);
        int r = dup_decompress(in, out, &len, &payload);
        abort(r == -1 || r == -2, UNITXT("Internal error, dup_decompress() = %p"), r);

        assert(c.size() > 0);
        payload_orig = c.at(0).payload;

        if (r == 0) {
            // dup_decompress() wrote literal at the destination, nothing we need to do
        } else if (r == 1) {
            // dup_decompress() returned a reference into a past written file
            size_t resolved = 0;
            while (resolved < len) {
                if (payload + resolved >= payload_orig && add_files) {
                    size_t fo = belongs_to(payload + resolved);
                    int j = io.seek(ofile, payload + resolved - payload_orig, SEEK_SET);
                    abort(j != 0, UNITXT("Internal error 1 or non-seekable device: seek(%s, %p, %p)"), infiles.at(fo).filename.c_str(), payload, payload_orig);
                    len2 = io.read(out + resolved, len - resolved, ofile);
                    abort(len2 != len - resolved, UNITXT("Internal error 2: read(%s, %p, %p)"), infiles.at(fo).filename.c_str(), len, len2);
                    resolved += len2;
                    io.seek(ofile, 0, SEEK_END);
                } else {
                    FILE *ifile2;
                    size_t fo = belongs_to(payload + resolved);
                    {
                        ifile2 = try_open(infiles.at(fo).filename, 'r', true);
                        infiles.at(fo).handle = ifile2;
                        int j = io.seek(ifile2, payload + resolved - infiles.at(fo).offset, SEEK_SET);
                        abort(j != 0, UNITXT("Internal error 9 or destination is a non-seekable device: seek(%s, %p, %p)"), infiles.at(fo).filename.c_str(), payload, infiles.at(fo).offset);
                    }
                    len2 = io.read(out + resolved, len - resolved, ifile2);
                    resolved += len2;
                    fclose(ifile2);
                }
            }
        } else {
            abort(true, UNITXT("Internal errror or source file corrupted: %d"), r);
        }

        uint64_t src_consumed = 0;

        while (c.size() > 0 && src_consumed < len) {
            if (ofile == 0) {
                ofile = create_file(c.at(0).extra);
                destfile = c.at(0).extra;
                checksum_init(&decompress_checksum);

                if (add_files) {
                    add_file(c.at(0).extra, add_file_payload);
                    add_file_payload += c.at(0).size;
                }

                curfile_written = 0;
            }

            auto missing = c.at(0).size - curfile_written;
            auto has = minimum(missing, len - src_consumed);
            curfile_written += has;
            update_statusbar_restore(destfile);
            io.try_write(out + src_consumed, has, ofile);
            checksum(out + src_consumed, has, &decompress_checksum);

            payload_written += has;
            src_consumed += has;

            if (curfile_written == c.at(0).size) {
                current_outfile_begin += c.at(0).size;

                io.close(ofile);
                ofile = 0;
                curfile_written = 0;
                abort(c.at(0).checksum != decompress_checksum.result, UNITXT("File checksum error"));

                c.erase(c.begin());
            }
        }
    }
}

void compress_symlink(const STRING &link, const STRING &target) {
    bool is_dir;
    STRING tmp;

    time_t file_modified = get_date(link).second;
    int t = symlink_target(link.c_str(), tmp, is_dir) ? 0 : -1;

    if (t == -1) {
        if (continue_flag) {
            statusbar.print(2, UNITXT("Skipped, error by readlink(): %s"), link.c_str());
        } else {
            abort(true, UNITXT("Aborted, error by readlink(): %s"), link.c_str());
        }
        return;
    }

    update_statusbar_backup(link + UNITXT(" -> ") + STRING(abs_path(tmp)));
    io.try_write("L", 1, ofile);

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
    write_contents_item(ofile, &c);
    contents.push_back(c);
    return;
}

uint64_t payload_compressed = 0; // Total payload returned by dup_compress() and flush_pend()
uint64_t payload_read = 0;       // Total payload read from disk
string payload_queue;            // Queue of payload read from disk. Can contain multiple small files
vector<contents_t> file_queue;

// NOTE! Remember to call with flush = true after, or with the last file! If you flush, then passing a file is optional
// and you can leave the string parameters empty.
void compress_file(const STRING &input_file, const STRING &filename, const bool flush) {
    auto empty_q = [&]() {
        if (payload_queue.size() > 0) {
            uint64_t pay;
            size_t cc = dup_compress(payload_queue.c_str(), (char *)out, payload_queue.size(), &pay);
            payload_compressed += pay;
            if (cc > 0) {
                io.try_write("A", 1, ofile);
                add_references(out, cc, io.write_count);
                io.try_write(out, cc, ofile);
                io.try_write("B", 1, ofile);
            }
            payload_queue.clear();
        }
    };

    auto flushit = [&]() {
        empty_q();
        while (payload_compressed < payload_read) {
            size_t pay;
            size_t cc = flush_pend((char *)out, &pay);
            payload_compressed += pay;
            if (cc > 0) {
                io.try_write("A", 1, ofile);
                add_references(out, cc, io.write_count);
                io.try_write(out, cc, ofile);
                io.try_write("B", 1, ofile);
            }
        }
        return;
    };

    if (flush && input_file == UNITXT("")) {
        flushit();
        return;
    }

    if (input_file != UNITXT("-stdin") && ISNAMEDPIPE(get_attributes(input_file, follow_symlinks)) && !named_pipes) {
        statusbar.print(2, UNITXT("Skipped, no -p flag for named pipes: %s"), input_file.c_str());
        return;
    }

    pair<time_t, time_t> file_time;
    int attributes = 0;
    checksum_t file_checksum;
    checksum_init(&file_checksum);
    uint64_t file_size = 0;
    contents_t file_meta;
    uint64_t file_read = 0;

    if (diff_flag && input_file != UNITXT("-stdin")) {
        if(!no_timestamp_flag) {
            file_time = get_date(input_file);
            contents_t f;
            auto it = contents_full.find(CASESENSE(abs_path(input_file)));
            // The "it->second.name == filename" is for Windows where we decide to do a full backup of a file if its only change was a case-rename. Note that
            // drive-letter casing can apparently fluctuate randomly on Windows, so don't compare full paths
            if(it != contents_full.end() && it->second.file_c_time == file_time.first && it->second.file_modified == file_time.second && it->second.name == filename) {
                update_statusbar_backup(input_file);
                contents_t c = it->second;
                c.unchanged = true;
                unchanged += c.size;
                unchanged_files++;
                contents.push_back(c);
                files++;
                // Could only be used if we supported restore of diff from stding
                // io.try_write("F", 1, ofile); 
                // write_contents_item(ofile, &c);
                if (flush) {
                    flushit();
                }
                return;
            }
        }
    }

    ifile = try_open(input_file.c_str(), 'r', false);

    if (!ifile) {
        if (continue_flag) {
            statusbar.print(2, UNITXT("Skipped, error opening source file: %s"), input_file.c_str());
            return;
        } else {
            abort(true, UNITXT("Aborted, error opening source file: %s"), input_file.c_str());
        }
    }

    update_statusbar_backup(input_file);

    if (input_file != UNITXT("-stdin")) {
        io.seek(ifile, 0, SEEK_END);
        file_time = get_date(input_file);
        file_size = io.tell(ifile);
        attributes = get_attributes(input_file, follow_symlinks);
        io.seek(ifile, 0, SEEK_SET);
    } else {
        file_time = {cur_date(), cur_date()};
        file_size = std::numeric_limits<uint64_t>::max();
    }

    file_meta.abs_path = abs_path(input_file);
    file_meta.payload = payload_read;
    file_meta.name = filename;
    file_meta.size = file_size;
    file_meta.file_c_time = file_time.first;
    file_meta.file_modified = file_time.second;
    file_meta.attributes = attributes;
    file_meta.directory = false;
    file_meta.symlink = false;
    file_meta.unchanged = false;
    checksum_init(&file_meta.ct);
    file_queue.push_back(file_meta);

    files++;

    if(!diff_flag) {
        io.try_write("F", 1, ofile);
        contents_t tmp = file_meta;
        tmp.abs_path.clear();
        write_contents_item(ofile, &tmp);
    }
    if (file_size > DISK_READ_CHUNK - payload_queue.size()) {
        empty_q();

        while (file_read < file_size) {
            update_statusbar_backup(input_file);
            size_t r = io.read_valid_length(in, minimum(file_size - file_read, DISK_READ_CHUNK), ifile, input_file);
            if (input_file == UNITXT("-stdin") && r == 0) {
                break;
            }
            file_read += r;
            payload_read += r;
            checksum(in, r, &file_meta.ct);
            payload_queue += string((char *)in, r);

            if (file_read == file_size && file_size > 0) {
                // No CRC block for 0-sized files
                io.try_write("C", 1, ofile);
                file_meta.checksum = file_meta.ct.result;
                io.write_ui<uint32_t>(file_meta.ct.result, ofile);
            }
            empty_q();
        }
        file_queue.clear();
    } else {
        assert(file_size <= DISK_READ_CHUNK - payload_queue.size());
        size_t r = io.read_valid_length(in, file_size, ifile, input_file);
        file_read += r;
        payload_read += r;
        checksum(in, r, &file_meta.ct);
        assert(file_read == file_size);
        if (file_read == file_size && file_size > 0) {
            // No CRC block for 0-sized files
            io.try_write("C", 1, ofile);
            file_meta.checksum = file_meta.ct.result;
            io.write_ui<uint32_t>(file_meta.ct.result, ofile);
        }
        payload_queue += string((char *)in, r);
    }

    fclose(ifile);

    if (flush) {
        flushit();
    }

    if (input_file == UNITXT("-stdin")) {
        file_meta.size = file_read;
    }

    file_meta.checksum = file_meta.ct.result;
    file_meta.file_id = file_id_counter;
    file_id_counter++;
    contents.push_back(file_meta);
}

bool lua_test(STRING path, const STRING &script, bool top_level) {
    if (script == UNITXT("")) {
        return true;
    }

    STRING dir;
    STRING file;
    uint64_t size = 0;
    STRING ext;
    STRING name;
    uint32_t attrib = 0;
    time_t date;
    int type;

#ifdef WINDOWS
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
    name = right(remove_delimitor(path)) == UNITXT("") ? path : right(remove_delimitor(path));
    size = filesize(path, false);

    size_t t = name.find_last_of(UNITXT("."));
    if (t != string::npos) {
        ext = name.substr(t + 1);
    } else {
        ext = UNITXT("");
    }
    return execute(script, path, type, name, size, ext, attrib, date, top_level);
}

bool include(const STRING &name, bool top_level) {
    STRING n = remove_delimitor(CASESENSE(unsnap(abs_path(name))));

    for (uint32_t j = 0; j < excludelist.size(); j++) {
        STRING e = excludelist.at(j);
        if (n == e) {
            // statusbar.print(9, UNITXT("Skipped, in -- exclude list: %s"), name.c_str());
            return false;
        }
    }

    if (!lua_test(name, lua, top_level)) {
        // statusbar.print(9, UNITXT("Skipped, by -f filter: %s"), name.c_str());
        return false;
    }
    return true;
}

void fail_list_dir(const STRING &dir) {
    if (continue_flag) {
        statusbar.print(2, UNITXT("Skipped, error listing directory: %s"), dir.c_str());
    } else {
        abort(true, UNITXT("Aborted, error listing directory: %s"), dir.c_str());
    }
}

void compress_recursive(const STRING &base_dir, vector<STRING> items, bool top_level) {
    // Todo, simplify this function by initially creating three distinct lists
    // for files, dirs and symlinks. Instead of iterating through the same list
    // with each their if-conditions

    vector<int> attributes;
    vector<STRING> root_items;
    vector<STRING> non_root_items;

    auto our_own = [](STRING file) {
        return output_file == UNITXT("-stdout") || ((diff.empty() || (!same_path(file, diff))) && !same_path(file, full));
    };

    // Sort input items so that root items are first.
    for (uint32_t i = 0; i < items.size(); i++) {
        if (items.at(i).find(DELIM_STR) == string::npos) {
            root_items.push_back(items.at(i));
        } else {
            non_root_items.push_back(items.at(i));
        }
    }

    items.clear();
    items.insert(items.end(), root_items.begin(), root_items.end());
    items.insert(items.end(), non_root_items.begin(), non_root_items.end());

    // Put item types (file, dir, symlink) in items_type[]
    // Todo, beautify by just deleting entries in 'items' instead of building an
    // 'items2'
    vector<STRING> items2;
    for (uint32_t i = 0; i < items.size(); i++) {
        STRING sub = base_dir + items.at(i);
        int type = get_attributes(sub, follow_symlinks);
        if (type == -1) {
            if (continue_flag) {
                statusbar.print(2, UNITXT("Skipped, access error: %s"), sub.c_str());
            } else {
                abort(true, UNITXT("Aborted, access error: %s"), sub.c_str());
            }
        } else {
            // avoid including full and diff file when compressing
            if (our_own(sub)) {
                items2.push_back(items.at(i));
                attributes.push_back(type);
            }
        }
    }
    items.clear();
    items.insert(items.end(), items2.begin(), items2.end());

    // Todo, rewrite to iterate through the list just once, and place items in each their new list. Then process
    // these new lists.

    // first process files
    for (uint32_t j = 0; j < items.size(); j++) {
        STRING sub = base_dir + items.at(j);
        if ((!ISDIR(attributes.at(j)) && !ISSOCK(attributes.at(j))) && !(ISLINK(attributes.at(j)) && !follow_symlinks) && include(sub, top_level)) {
            save_directory(base_dir, left(items.at(j)) + (left(items.at(j)) == UNITXT("") ? UNITXT("") : DELIM_STR), true);
            STRING u = items.at(j);
            STRING s = right(u) == UNITXT("") ? u : right(u);
            compress_file(sub, s, false);
        }
    }

    // then process symlinks
    for (uint32_t j = 0; j < items.size(); j++) {
        STRING sub = base_dir + items.at(j);

        if (ISLINK(attributes.at(j)) && !follow_symlinks && include(sub, top_level)) {
            // avoid including full and diff file when compressing
            if (our_own(sub)) {
                save_directory(base_dir, left(items.at(j)) + (left(items.at(j)) == UNITXT("") ? UNITXT("") : DELIM_STR), true);
                compress_symlink(sub, right(items.at(j)) == UNITXT("") ? items.at(j) : right(items.at(j)));
            }
        }
    }

    // finally process directories
    for (uint32_t j = 0; j < items.size(); j++) {
        STRING sub = base_dir + items.at(j);
        if (ISDIR(attributes.at(j)) && (!no_recursion_flag || top_level) && include(sub, top_level)) {
            if (items.at(j) != UNITXT("")) {
                items.at(j) = remove_delimitor(items.at(j)) + DELIM_STR;
            }

            vector<STRING> newdirs;
#ifdef WINDOWS
            if (ISLINK(attributes.at(j))) {
                continue;
            }

            HANDLE hFind;
            BOOL bContinue = TRUE;
            WIN32_FIND_DATAW data;
            STRING s = remove_delimitor(sub) + UNITXT("\\*");
            hFind = FindFirstFileW(s.c_str(), &data);
            bContinue = hFind != INVALID_HANDLE_VALUE;

            if (hFind == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) {
                fail_list_dir(sub);
            } else {
                while (bContinue) {
                    if (STRING(data.cFileName) != UNITXT(".") && STRING(data.cFileName) != UNITXT("..")) {
                        newdirs.push_back(items.at(j) + STRING(data.cFileName));
                    }
                    bContinue = FindNextFileW(hFind, &data);
                }
                FindClose(hFind);
            }
#else
            struct dirent *entry;
            DIR *dir = opendir(sub.c_str());

            if (dir == 0) {
                fail_list_dir(sub);
            } else {
                while ((entry = readdir(dir)) != 0) {
                    if (STRING(entry->d_name) != UNITXT(".") && STRING(entry->d_name) != UNITXT("..")) {
                        newdirs.push_back(items.at(j) + entry->d_name);
                    }
                }
            }

            closedir(dir);
#endif
            if (items.at(j) != UNITXT("")) {
                dirs++;
            }
            save_directory(base_dir, items.at(j), true);
            compress_recursive(base_dir, newdirs, false);
        }
    }
}

void compress(const STRING &base_dir, vector<STRING> items) {
    compress_recursive(base_dir, items, true);   // calls compress_file() with flush parameter = false
    compress_file(UNITXT(""), UNITXT(""), true); // flush at the end - VERY important!
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
    if (base_dir != UNITXT("")) {
        base_dir += DELIM_CHAR;
    }
    statusbar.m_base_dir = base_dir;

    for (i = 0; i < args.size(); i++) {
        args.at(i) = args.at(i).substr(base_dir.length());
    }

    compress(base_dir, args);
}

void decompress_sequential(const STRING &extract_dir, bool add_files) {
    STRING curdir;
    size_t r = 0;
    STRING base_dir = abs_path(extract_dir);
    statusbar.m_base_dir = base_dir;

    curdir = extract_dir;
    // ensure_relative(curdir);
    save_directory(UNITXT(""), curdir + DELIM_STR); // initial root

    for (;;) {
        char w;

        r = io.try_read_buf(&w, 1, ifile);
        abort(r == 0, UNITXT("Unexpected end of archive (block tag)"));

        if (w == 'I') {
            contents_t c;
            read_content_item(ifile, &c);
            ensure_relative(c.name);
            curdir = extract_dir + DELIM_STR + c.name;
            save_directory(UNITXT(""), curdir);
            create_directories(curdir, c.file_modified);
        } else if (w == 'F') {
            contents_t c;
            files++;
            read_content_item(ifile, &c);
            STRING buf2 = remove_delimitor(curdir) + DELIM_STR + c.name;

            if (c.size == 0) {
                // May not have a corrosponding data block ('A' block) to trigger decompress_files()
                FILE *h = create_file(buf2);
                files++;
                io.close(h);
            } else {
                c.extra = buf2;
                c.checksum = 0;
                file_queue.push_back(c);
                update_statusbar_restore(buf2);
                name = c.name;
            }
        } else if (w == 'A') {
            decompress_files(file_queue, add_files);
        } else if (w == 'C') { // crc
            uint32_t crc = io.read_ui<uint32_t>(ifile);
            file_queue.at(file_queue.size() - 1).checksum = crc;
        } else if (w == 'L') { // symlink
            contents_t c;
            files++;
            read_content_item(ifile, &c);
            STRING buf2 = curdir + DELIM_CHAR + c.name;
            create_symlink(buf2, c);
        }

        else if (w == 'X') {
            return;
        } else {
            abort(true, UNITXT("Source file corrupted"));
        }
    }
}

void write_header(FILE *file, status_t s, uint64_t mem, bool hash_flag, uint64_t hash_salt, uint64_t archive_id) {
    if (s == BACKUP) {
        io.try_write("EXDUPE F", 8, file);
    } else if (s == DIFF_BACKUP) {
        io.try_write("EXDUPE D", 8, file);
    } else {
        abort(true, UNITXT("Internal error 25"));
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
    string header = io.try_read(8, file);
    if (action == BACKUP) {
        abort(header != "EXDUPE F", UNITXT("'%s' is not a .full file"), filename.c_str());

    } else if (action == DIFF_BACKUP) {
        abort(header != "EXDUPE D", UNITXT("'%s' is not a .diff file"), filename.c_str());
    } else {
        abort(true, UNITXT("Internal error 278"));
    }

    char major = io.read_ui<uint8_t>(file);
    char minor = io.read_ui<uint8_t>(file);
    char revision = io.read_ui<uint8_t>(file);
    char dev = io.read_ui<uint8_t>(file);

    if (major == 1 && minor == 1 && revision == 0 && dev == 0) {
        abort(true, UNITXT("This file was created with eXdupe version 1.1.0.dev-23 or earlier. Please use the exact same dev-version on it"));
    }

    uint64_t id = io.read_ui<uint64_t>(file);
    if(archive_id) {
        *archive_id = id;
    }

    DEDUPE_SMALL = io.read_ui<uint64_t>(file);
    DEDUPE_LARGE = io.read_ui<uint64_t>(file);

    abort(dev != VER_DEV, UNITXT("This file was created with eXdupe version %d.%d.%d.dev-%d. Please use the exact same version on it"), major, minor, revision, dev);

    hash_flag = io.read_ui<uint8_t>(file) == 1;
    hash_salt = io.read_ui<uint64_t>(file);

    return io.read_ui<uint64_t>(file); // mem usage
}

void wrote_message(uint64_t bytes, uint64_t files) { statusbar.print(1, UNITXT("Wrote %s bytes in %s files"), del(bytes).c_str(), del(files).c_str()); }

void create_shadows(void) {
#ifdef WINDOWS
    shadow(shadows);

    vector<pair<STRING, STRING>> snaps; //(STRING mount, STRING shadow)
    snaps = get_snaps();
    for (uint32_t i = 0; i < snaps.size(); i++) {
        statusbar.print(3, UNITXT("Created snapshot %s -> %s"), snaps.at(i).first.c_str(), snaps.at(i).second.c_str());
    }
#endif
}


#ifdef WINDOWS
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
    if (argc2 == 2 && argv.at(1) == UNITXT("-u?")) {
        print_lua_help();
        return 0;
    }

    if (argc2 == 2 && argv.at(1) == UNITXT("-?")) {
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

#ifdef WINDOWS
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_U16TEXT);
#endif
    statusbar.m_verbose_level = verbose_level;

    if (restore_flag || compress_flag || list_flag) {
        in = static_cast<unsigned char *>(tmalloc(DISK_READ_CHUNK + M));
        out = static_cast<unsigned char *>(tmalloc((threads + 1) * DISK_READ_CHUNK + M)); // todo, compute exact to save memory
    }

    if (list_flag) {
        dump_contents();
    } else if (restore_flag && full != UNITXT("-stdin") && diff != UNITXT("-stdin")) {
        // Restore from file.
        // =================================================================================================
        if (diff_flag) {
            FILE *fdiff = try_open(diff, 'r', true);
            FILE *ffull = try_open(full, 'r', true);
            uint64_t full_id;
            uint64_t diff_id;
            read_header(ffull, full, BACKUP, &full_id);
            read_header(fdiff, diff, DIFF_BACKUP, &diff_id);
            abort(full_id != diff_id, UNITXT("The diff file does not belong to the full file. "));
            init_content_maps(ffull);

            decompress_individuals(ffull, fdiff);
        } else {
            ifile = try_open(full, 'r', true);
            read_header(ifile, full, BACKUP);
            decompress_individuals(ifile, ifile);
        }
        wrote_message(tot_res, files);
    } else if ((restore_flag && (full == UNITXT("-stdin"))) && restorelist.size() == 0) {
        // Restore from stdin. Only entire archive can be restored this way
        STRING s = remove_delimitor(directory);
        ifile = try_open(full, 'r', true);
        read_header(ifile, full, BACKUP);
        decompress_sequential(s, true);
        assert(!diff_flag);
        wrote_message(io.write_count, files);

        // read remainder of file like content section, etc, to avoid error from OS
        vector<std::byte> tmp(32 * 1024, {});
        while (ifile == stdin && io.read(tmp.data(), 32 * 1024, stdin)) {
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
            abort(!hashtable, UNITXT("Out of memory. This differential backup requires %d MB. Try -t1 flag"), memory_usage >> 20);
            memset(hashtable, 0, memory_usage);
            pay_count = read_references(ifile); // read size in bytes of user payload in .full file
            references.clear();

            int r = dup_init(DEDUPE_LARGE, DEDUPE_SMALL, memory_usage, threads, hashtable, compression_level, hash_flag, hash_salt, pay_count);
            abort(r == 1, UNITXT("Out of memory. This differential backup requires %d MB. Try -t1 flag"), memory_usage >> 20);
            abort(r == 2, UNITXT("Error creating threads. This differential backup requires %d MB memory. Try -t1 flag"), memory_usage >> 20);

            read_hashtable(ifile);

            auto con = read_contents(ifile);
            for(auto c : con) {
                c.abs_path = CASESENSE(c.abs_path);
                contents_full[CASESENSE(abs_path(c.abs_path))] = c;
            }

            io.close(ifile);

        } else {
            archive_id = rnd64();
            output_file = full;
            ofile = create_file(output_file);
            hash_salt = hash_flag ? rnd64() : 0;
            hashtable = tmalloc(memory_usage);
            abort(!hashtable, UNITXT("Out of memory. Reduce -m, -g or -t flag"));
            int r = dup_init(DEDUPE_LARGE, DEDUPE_SMALL, memory_usage, threads, hashtable, compression_level, hash_flag, hash_salt, 0);
            abort(r == 1, UNITXT("Out of memory. Reduce -m, -g or -t flag"));
            abort(r == 2, UNITXT("Error creating threads. Reduce -m, -g or -t flag"));
        }

        output_file_mine = true; // todo, can this be deleted?
        write_header(ofile, diff_flag ? DIFF_BACKUP : BACKUP, memory_usage, hash_flag, hash_salt, archive_id);

        if (inputfiles.size() > 0 && inputfiles.at(0) != UNITXT("-stdin")) {
            compress_args(inputfiles);
        } else if (inputfiles.size() > 0 && inputfiles.at(0) == UNITXT("-stdin")) {
            name = UNITXT("stdin");
            compress_file(UNITXT("-stdin"), name, true); // flush = true
        }

        if (files + dirs == 0) {
            if (no_recursion_flag) {
                // todo, delete, wildcard no longer needed
                abort(true, UNITXT("0 source files or directories. Missing '*' wildcard with -r flag?"));
            } else {
                abort(true, UNITXT("0 source files or directories"));
            }
        }

        io.try_write("X", 1, ofile);
        write_contents(ofile);
        size_t hashtable_size = 0;

        if (!diff_flag) {
            hashtable_size = write_hashtable(ofile);
        }

        size_t references_size = write_references(ofile);
        io.try_write("END", 3, ofile);
        if(verbose_level > 0 && verbose_level < 3) {
            statusbar.clear_line();
        }
        if (statistics_flag) {  
            std::ostringstream s;
            uint64_t t = (GetTickCount() - start_time);
            t = t == 0 ? 1 : t;
            int sratio = int((double(io.write_count) / double(dup_counter_payload() + unchanged + 1)) * 100.);
            sratio = sratio > 999 ? 999 : sratio == 0 ? 1 : sratio;
            statusbar.print_no_lf(1, UNITXT("Compressed %s B in %s files into %s B (%s%%) at %sB/s\n"), del(dup_counter_payload() + unchanged).c_str(), del(files).c_str(), del(io.write_count).c_str(), s2w(std::to_string(sratio)).c_str(), s2w(format_size((dup_counter_payload() + unchanged) / t * 1000)).c_str());
            s << "Unchanged files:             " << w2s(del(unchanged)) << " B in " << w2s(del(unchanged_files)) << " files\n";
            s << "Stored as literals:          " << format_size(stored_as_literals) << "B (" << format_size(literals_compressed_size) << "B compressed)\n";
            s << "Stored as duplicated blocks: " << format_size(largehits + smallhits) << "B (" << format_size(largehits) << "B large, " << format_size(smallhits) << "B small)\n";
            uint64_t total = literals_compressed_size + contents_size + references_size + hashtable_size;
            s << "Overheads:                   " << format_size(contents_size) << "B meta, " << format_size(references_size) << "B refs, " << format_size(hashtable_size) << "B hashtable, " << format_size(io.write_count - total) << "B misc\n";    
            s << "Unhashed due to congestion:  " << format_size(congested_large) << "B large, " << format_size(congested_small) << "B small\n";
            s << "Unhashed anomalies:          " << format_size(anomalies_large) << "B large, " << format_size(anomalies_small) << "B small";
            STRING str = s2w(s.str());
            statusbar.print(0, UNITXT("%s"), str.c_str());
            wcerr << "Hashtable fillratio:         ";
            print_fillratio();            
        }
        else {
            statusbar.print_no_lf(1, UNITXT("Compressed %s B in %s files into %sB\n"), del(dup_counter_payload() + unchanged).c_str(), del(files).c_str(), s2w(format_size(io.write_count)).c_str());
        }

        io.close(ofile);
    } else {
        print_usage(false);
    }

#ifdef WINDOWS
    unshadow();
#endif
    return 0;
}
