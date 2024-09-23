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

// NOT thread safe
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
    size_t read(void *_DstBuf, size_t _Count, FILE *_File);
    size_t write(const void *_Str, size_t _Count, FILE *_File);
    size_t try_write(const void *Str, size_t Count, FILE *_File);
    size_t try_read_buf(void *DstBuf, size_t Count, FILE *_File);
    size_t read_valid_length(void *DstBuf, size_t Count, FILE *_File, STRING name);
    STRING readstr(FILE *_File);
    void writestr(STRING str, FILE *_File);
    std::string try_read(size_t Count, FILE *_File);

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
        try_read_buf(buf, size, _File);
        T value = 0;

        for (std::size_t i = 0; i < size; ++i) {
            value <<= 8;
            value |= static_cast<T>(buf[size - 1 - i]);
        }     
        return value;
    }

    template <typename T> requires std::is_unsigned_v<T> size_t encode_compact(T value, uint8_t* encodedBytes) {
        size_t size = 0;
        while (value >= 0x80) {
            encodedBytes[size++] = static_cast<uint8_t>(value & 0x7F) | 0x80;
            value >>= 7;
        }
        encodedBytes[size++] = static_cast<uint8_t>(value);
        return size;
    }

    template <typename T> requires std::is_unsigned_v<T> T decode_compact(const uint8_t* encodedBytes) {
        T result = 0;
        int shift = 0;

        for (uint8_t byte : encodedBytes) {
            result |= (static_cast<T>(byte & 0x7F) << shift);
            shift += 7;

            if ((byte & 0x80) == 0) {
                break;
            }
        }

        return result;
    }


    template <typename T> requires std::is_unsigned_v<T> void write_compact(T value, FILE* f) {
        uint8_t buf[20];
        size_t size = encode_compact(value, buf);
        try_write(buf, size, f);
    }

    template <typename T> requires std::is_unsigned_v<T> T read_compact(FILE* f) {
        T result = 0;
        int shift = 0;

        for(;;) {
            uint8_t byte;
            try_read_buf(&byte, 1, f);
            result |= (static_cast<T>(byte & 0x7F) << shift);
            shift += 7;

            if ((byte & 0x80) == 0) {
                break;
            }
        }

        return result;
    }

    static bool stdin_tty();

};
#endif
