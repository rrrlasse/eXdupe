// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold


#ifndef AIO_HEADER
#define AIO_HEADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstdint>
#ifdef WINDOWS
#include <windows.h>
#endif

#include "utilities.hpp"
#include "unicode.h"

using namespace std;

class Cio {
  private:
    char tmp[4096];
    wchar_t wtmp[4096];
    CHR Ctmp[4096];

  public:
    uint64_t read_count;
    uint64_t write_count;

    Cio();
    //	void Cio::ahead(STRING file);
    int close(FILE *_File);
    FILE *open(STRING file, char mode);
    uint64_t tell(FILE *_File);
    int seek(FILE *_File, int64_t _Offset, int Origin);
    bool write_date(struct tm *t, FILE *_File);
    bool read_date(struct tm *t, FILE *_File);
    size_t read(void *_DstBuf, size_t _Count, FILE *_File);
    size_t write(const void *_Str, size_t _Count, FILE *_File);
    size_t try_write(const void *Str, size_t Count, FILE *_File);
    size_t try_read(void *DstBuf, size_t Count, FILE *_File);
    size_t read_valid_length(void *DstBuf, size_t Count, FILE *_File, STRING name);
    STRING readstr(FILE *_File);
    size_t writestr(STRING str, FILE *_File);

    template <std::unsigned_integral T> size_t write_ui(T value, FILE* _File) {
        const std::size_t size = sizeof(T);
        std::uint8_t buf[size];

        for (std::size_t i = 0; i < size; ++i) {
            buf[i] = static_cast<uint8_t>(value);
            value >>= 8;
        }
        size_t r = try_write(buf, size, _File);
        return r;
    }

    template <std::unsigned_integral T> T read_ui(FILE* _File) {
        const std::size_t size = sizeof(T);
        std::uint8_t buf[size];
        try_read(buf, size, _File);
        T value = 0;

        for (std::size_t i = 0; i < size; ++i) {
            value <<= 8;
            value |= static_cast<T>(buf[size - 1 - i]);
        }     
        return value;
    }

    static bool stdin_tty();

};
#endif
