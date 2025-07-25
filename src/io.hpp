// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2025: Lasse Mikkel Reinhold

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

#include "gsl/gsl"

#include "utilities.hpp"
#include "unicode.h"

// NOT thread safe
class Cio {
  public:
    Cio();
    int close(FILE *_File);
    FILE *open(STRING file, char mode);
    uint64_t tell(FILE *_File);
    int seek(FILE *_File, int64_t _Offset, int Origin);
    size_t write(const void *Str, size_t Count, FILE *_File);
    size_t read(void *DstBuf, size_t Count, FILE *_File, bool read_exact = true);
    size_t read_vector(std::vector<char>& dst, size_t count, size_t offset, FILE* f, bool read_exact);
    STRING read_utf8_string(FILE *_File);
    void write_utf8_string(STRING str, FILE *_File);
    std::string read_bin_string(size_t Count, FILE *_File);
    void truncate(FILE *file);

    static bool stdin_tty();
    
    uint64_t read_count;
    uint64_t write_count;

    template <std::unsigned_integral T> size_t write_ui(T value, FILE* _File) {
        uint64_t v = value;
        const std::size_t size = sizeof(T);
        std::uint8_t buf[size];

        for (std::size_t i = 0; i < size; ++i) {
            buf[i] = static_cast<uint8_t>(v);
            v >>= 8;
        }
        size_t r = write(buf, size, _File);
        return r;
    }

    template <std::unsigned_integral T> T read_ui(FILE* _File) {
        const std::size_t size = sizeof(T);
        std::uint8_t buf[size];
        read(buf, size, _File);
        uint64_t value = 0;

        for (std::size_t i = 0; i < size; ++i) {
            value <<= 8;
            value |= static_cast<T>(buf[size - 1 - i]);
        }     
        return gsl::narrow<T>(value);
    }

    template <typename T> requires std::is_unsigned_v<T> size_t encode_compact(T value, uint8_t* dst) {
        size_t size = 0;
        while (value >= 0x80) {
            dst[size++] = static_cast<uint8_t>(value & 0x7F) | 0x80;
            value >>= 7;
        }
        dst[size++] = static_cast<uint8_t>(value);
        return size;
    }

    template <typename T> requires std::is_unsigned_v<T> T decode_compact(const uint8_t * src) {
        T result = 0;
        int shift = 0;
        constexpr int max_bytes = (sizeof(T) * 8 + 6) / 7;
        for (int i = 0; i < max_bytes; ++i) {
            uint8_t byte = *src++;
            result |= (static_cast<T>(byte & 0x7F) << shift);
            if ((byte & 0x80) == 0) {
                return result;
            }
            shift += 7;
        }
        rassert(false, result, shift);
    }

    template <typename T> requires std::is_unsigned_v<T> void write_compact(T value, FILE* f) {
        uint8_t buf[20];
        size_t size = encode_compact(value, buf);
        write(buf, size, f);
    }

    template <typename T> requires std::is_unsigned_v<T> T read_compact(FILE* f) {
        T result = 0;
        int shift = 0;

        for(;;) {
            uint8_t byte;
            read(&byte, 1, f);
            result |= (static_cast<T>(byte & 0x7F) << shift);
            shift += 7;

            if ((byte & 0x80) == 0) {
                break;
            }
        }

        return result;
    }


};
