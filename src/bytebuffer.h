// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#pragma once

#include <cstddef>
#include <stdint.h>

typedef struct {
    uint64_t pay;
    size_t len;
    char *buffer_offset;
} buffer_t;

class Bytebuffer {
  public:
    Bytebuffer(size_t size);
    void buffer_add(const unsigned char *src, uint64_t payload, size_t len);
    char *buffer_find(uint64_t payload, size_t len);

  private:
    std::vector<char> m_buffer;
    std::vector<buffer_t> m_buffers;
};
