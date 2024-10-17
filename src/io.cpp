// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include <iostream>
#include <time.h>

#include "io.hpp"
#include "unicode.h"
#include "utilities.hpp"

#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64)
#include <io.h>
#define WINDOWS
#endif

#ifndef WINDOWS
#if defined(hpux) || defined(__hpux) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__FreeBSD__)
#define _ftelli64 ftello
#define _fseeki64 fseeko
#elif defined(__APPLE__)
#define _ftelli64 ftell
#define _fseeki64 fseek
#else
#define _ftelli64 ftello64
#define _fseeki64 fseeko64
#endif
#endif

Cio::Cio() {
    write_count = 0;
    read_count = 0;
}

// Only way I could find that detected both pipes and redirection. Todo, is this OK?
bool Cio::stdin_tty() {
#ifdef _WIN32
    return _isatty(0);
#else
    return isatty(0);
#endif
}

int Cio::close(FILE *_File) { return fclose(_File); }

FILE *Cio::open(STRING file, char mode) {
    if (mode == 'r') {
        STRING s = slashify(file);
        FILE *f = FOPEN(s.c_str(), UNITXT("rb"));
        return f;
    } else {
        return FOPEN(file.c_str(), UNITXT("wb+"));
    }
}

uint64_t Cio::tell(FILE *_File) { return _ftelli64(_File); }

int Cio::seek(FILE *_File, int64_t _Offset, int Origin) { return _fseeki64(_File, _Offset, Origin); }

size_t Cio::write(const void *Str, size_t Count, FILE *_File) {
    size_t c = 0;
    while (c < Count) {
        size_t w = minimum(Count - c, 1024 * 1024);
        size_t r = fwrite((char*)Str + c, 1, w, _File);
        write_count += r;
        abort(r != w, UNITXT("Disk full or write denied while writing destination file"));
        c += r;
    }
    return Count;
}

size_t Cio::read(void* DstBuf, size_t Count, FILE* _File, bool read_exact) {
    size_t c = 0;
    for(;;) {
        size_t r = minimum(Count - c, 1024 * 1024);
        size_t w = fread((char*)DstBuf + c, 1, r, _File);
        read_count += w;
        abort(read_exact && stdin_tty() && w != r, UNITXT("Unexpected end of source file"));
        c += w;
        if(c == Count || w != r) {
            break;
        }
    }
    return c;
}

size_t Cio::read_vector(std::vector<char>& dst, size_t count, size_t offset, FILE* f, bool read_exact) {
    if(dst.size() < count + offset) {
        dst.resize(count + offset);
    }
    return read(dst.data() + offset, count, f, read_exact);
}


std::string Cio::read_bin_string(size_t Count, FILE *_File) {
    std::string str(Count, 'c');
    if(Count > 0) {
        size_t r = Cio::read(&str[0], Count, _File);
        abort(stdin_tty() && r != Count, UNITXT("Unexpected end of source file"));
    }
    return str;
}

STRING Cio::read_utf8_string(FILE *_File) {
    int t = read_compact<uint64_t>(_File);
    std::string tmp = read_bin_string(t, _File);
#ifdef WINDOWS
    int req = MultiByteToWideChar(CP_UTF8, 0, tmp.c_str(), -1, nullptr, 0);
    wstring res(req, 'c');
    MultiByteToWideChar(CP_UTF8, 0, tmp.c_str(), -1, &res[0], t);
    res.pop_back(); // WideCharToMultiByte() adds trailing zero
    return res;
#else
    return tmp;
#endif
}

void Cio::write_utf8_string(STRING str, FILE *_File) {
#ifdef WINDOWS
    size_t req = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> v(req, L'c');
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &v[0], static_cast<int>(req), 0, 0);
    req--; // WideCharToMultiByte() adds trailing zero
    write_compact<uint64_t>(static_cast<uint64_t>(req), _File); // fixme, gsl::narrow
    write(&v[0], req, _File);
#else
    write_compact<uint64_t>(str.size(), _File);
    write(str.c_str(), str.size(), _File);
#endif
}