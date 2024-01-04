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

size_t Cio::try_read(void *DstBuf, size_t Count, FILE *_File) {
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

bool Cio::write_date(tm *t, FILE *_File) {
    write_ui<uint8_t>(t->tm_hour, _File);
    write_ui<uint8_t>(t->tm_isdst, _File);
    write_ui<uint8_t>(t->tm_mday, _File);
    write_ui<uint8_t>(t->tm_min, _File);
    write_ui<uint8_t>(t->tm_mon, _File);
    write_ui<uint8_t>(t->tm_sec, _File);
    write_ui<uint8_t>(t->tm_wday, _File);
    write_ui<uint16_t>(t->tm_yday, _File);
    write_ui<uint16_t>(t->tm_year, _File);
    return true;
}

bool Cio::read_date(tm *t, FILE *_File) {
    t->tm_hour = read_ui<uint8_t>(_File);
    t->tm_isdst = read_ui<uint8_t>(_File);
    t->tm_mday = read_ui<uint8_t>(_File);
    t->tm_min = read_ui<uint8_t>(_File);
    t->tm_mon = read_ui<uint8_t>(_File);
    t->tm_sec = read_ui<uint8_t>(_File);
    t->tm_wday = read_ui<uint8_t>(_File);
    t->tm_yday = read_ui<uint16_t>(_File);
    t->tm_year = read_ui<uint16_t>(_File);
    return true;
}

// Todo, these functions are badly written
STRING Cio::readstr(FILE *_File) {
    char tmp3[MAX_PATH_LEN];
    memset(tmp3, 0, MAX_PATH_LEN);
    int t = read_ui<uint16_t>(_File);
    abort(t > MAX_PATH_LEN, UNITXT("Internal error, attempted to read a string longer than MAX_PATH_LEN"));
    try_read(tmp3, t, _File);
#ifdef WINDOWS
    wchar_t tmp2[MAX_PATH_LEN];
    memset(tmp2, 0, MAX_PATH_LEN);
    MultiByteToWideChar(CP_UTF8, 0, tmp3, -1, tmp2, t);
    return STRING(tmp2);
#else
    return STRING(tmp3);
#endif
}

size_t Cio::writestr(STRING str, FILE *_File) {
    abort(str.size() > MAX_PATH_LEN, UNITXT("Internal error, attempted to write a string longer than MAX_PATH_LEN"));
    CHR tmp3[MAX_PATH_LEN];
    memset(tmp3, 0, MAX_PATH_LEN);
    char tmp2[2*MAX_PATH_LEN];

#ifdef WINDOWS
    memset(tmp2, 0, MAX_PATH_LEN);
    size_t t = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, tmp2, MAX_PATH_LEN, 0, 0);
#else
    size_t t = str.length();
    memcpy(tmp2, str.c_str(), str.length());
#endif

    size_t r = write_ui<uint16_t>((unsigned int)t, _File);
    r += try_write(tmp2, t, _File);
    return r;
}