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

size_t Cio::read(void *_DstBuf, size_t _Count, FILE *_File) {
    size_t r = fread(_DstBuf, 1, _Count, _File);
    read_count += r;
    return r;
}

size_t Cio::write(const void *_Str, size_t _Count, FILE *_File) {
#if 0
	for (int i = 0; i < _Count; i++) {
		unsigned char c = ((unsigned char*)_Str)[i];
		if (c >= 32) {
			std::cerr << c;
		}
		else {
			std::cerr << ".";
		}
	}
#endif

    size_t w = fwrite(_Str, 1, _Count, _File);
    write_count += w;
    return w;
}

size_t Cio::try_write(const void *Str, size_t Count, FILE *_File) {
    size_t c = 0;
    while (c < Count) {
        size_t w = minimum(Count - c, 512 * 1024);
        size_t r = Cio::write((char *)Str + c, w, _File);
        abort(r != w, UNITXT("Disk full or write denied while writing destination file"));
        c += r;
    }
    return Count;
}

std::string Cio::try_read(size_t Count, FILE *_File) {
    std::string str(Count, 'c');
    if(Count > 0) {
        size_t r = Cio::try_read_buf(&str[0], Count, _File);
        abort(stdin_tty() && r != Count, UNITXT("Unexpected end of source file"));
    }
    return str;
}

size_t Cio::try_read_buf(void *DstBuf, size_t Count, FILE *_File) {
    size_t c = 0;
    while (c < Count) {
        size_t r = minimum(Count - c, 512 * 1024);
        size_t w = Cio::read((char *)DstBuf + c, r, _File);
        abort(stdin_tty() && w != r, UNITXT("Unexpected end of source file"));
        c += r;
    }
    return Count;
}

// Call if you have prior tested that the file is long enough that the read will not exceed it
size_t Cio::read_valid_length(void *DstBuf, size_t Count, FILE *_File, STRING name) {
    size_t w = Cio::read((char *)DstBuf, Count, _File);
    // Can be caused by region-locked files if on Windows, where it can occur anywhere inside
    // the file. We do not want to attempt to discard compressed data that has already been written
    // to the destination file (this is even impossible if compressing to stdout). So just abort. 
    abort(stdin_tty() && w != Count, (UNITXT("Error reading file that has been opened successfully for reading - cannot recover: ") + name).c_str());

    return w;
}


STRING Cio::readstr(FILE *_File) {
    int t = read_compact<uint16_t>(_File);
    std::string tmp = try_read(t, _File);
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

void Cio::writestr(STRING str, FILE *_File) {
#ifdef WINDOWS
    size_t req = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    abort(req > std::numeric_limits<uint16_t>::max(), UNITXT("Internal error, attempted to write a string longer than 65535"));
    std::vector<char> v(req, L'c');
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &v[0], static_cast<int>(req), 0, 0);
    req--; // WideCharToMultiByte() adds trailing zero
    write_compact<uint16_t>(static_cast<uint16_t>(req), _File); // todo, gsl::narrow
    try_write(&v[0], req, _File);
#else
    write_ui<uint16_t>(str.size(), _File);
    try_write(str.c_str(), str.size(), _File);
#endif
}