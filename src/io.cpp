// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2023: Lasse Mikkel Reinhold

#include <iostream>
#include <time.h>

#include "io.hpp"
#include "unicode.h"
#include "utilities.hpp"

#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64)
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
        abort(w != r, UNITXT("Unexpected end of source file"));
        c += r;
    }
    return Count;
}

size_t Cio::write64(uint64_t i, FILE *_File) {
    uint64_t j = i;
    char c[8];
    for (unsigned int k = 0; k < 8; k++) {
        c[k] = (char)j;
        j >>= 8;
    }
    size_t r = try_write(c, 8, _File);
    return r;
}

size_t Cio::write32(unsigned int i, FILE *_File) {
    unsigned int j = i;
    char c[4];
    for (unsigned int k = 0; k < 4; k++) {
        c[k] = (char)j;
        j >>= 8;
    }
    size_t r = try_write(c, 4, _File);
    return r;
}

unsigned int Cio::read32(FILE *_File) {
    unsigned int j = 0;
    unsigned char c[4];
    try_read(c, 4, _File);
    for (unsigned int k = 0; k < 4; k++) {
        j = j << 8;
        j = (j | c[3 - k]);
    }
    return j;
}

bool Cio::write_date(tm *t, FILE *_File) {
    write32(t->tm_hour, _File);
    write32(t->tm_isdst, _File);
    write32(t->tm_mday, _File);
    write32(t->tm_min, _File);
    write32(t->tm_mon, _File);
    write32(t->tm_sec, _File);
    write32(t->tm_wday, _File);
    write32(t->tm_yday, _File);
    write32(t->tm_year, _File);
    return true;
}

bool Cio::read_date(tm *t, FILE *_File) {
    t->tm_hour = read32(_File);
    t->tm_isdst = read32(_File);
    t->tm_mday = read32(_File);
    t->tm_min = read32(_File);
    t->tm_mon = read32(_File);
    t->tm_sec = read32(_File);
    t->tm_wday = read32(_File);
    t->tm_yday = read32(_File);
    t->tm_year = read32(_File);
    return true;
}

size_t Cio::write8(char i, FILE *_File) {
    size_t r = try_write(&i, 1, _File);
    return r;
}

char Cio::read8(FILE *_File) {
    char c;
    try_read(&c, 1, _File);
    return c;
}

uint64_t Cio::read64(FILE *_File) {
    uint64_t j = 0;
    unsigned char c[8];
    try_read(c, 8, _File);

    for (unsigned int k = 0; k < 8; k++) {
        j = j << 8;
        j = j | c[7 - k];
    }
    return j;
}

STRING Cio::readstr(FILE *_File) {
    char tmp3[MAX_PATH_LEN];
    memset(tmp3, 0, MAX_PATH_LEN);
    int t = read32(_File);
    try_read(tmp3, t, _File); // read string
#ifdef WINDOWS
    wchar_t tmp2[MAX_PATH_LEN];
    memset(tmp2, 0, MAX_PATH_LEN);
    MultiByteToWideChar(CP_UTF8, 0, tmp3, -1, tmp2, t);
    return STRING(tmp2);
#else
    return STRING(tmp);
#endif
}

size_t Cio::writestr(STRING str, FILE *_File) {
    CHR tmp3[MAX_PATH_LEN];
    memset(tmp3, 0, MAX_PATH_LEN);
    char tmp2[4096];

#ifdef WINDOWS
    memset(tmp2, 0, MAX_PATH_LEN);
    size_t t = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, tmp2, MAX_PATH_LEN, 0, 0);
#else
    size_t t = str.length();
    memcpy(tmp2, str.c_str(), str.length());
#endif

    size_t r = write32((unsigned int)t, _File);
    r += try_write(tmp2, t, _File);
    return r;
}
