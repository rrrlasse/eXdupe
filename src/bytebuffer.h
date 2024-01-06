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
    uint64_t offset;
    std::vector<unsigned char> data;
} buffer_t;

class Bytebuffer {
  public:
    Bytebuffer(size_t size);
    void buffer_add(const unsigned char *src, uint64_t payload, size_t len);
    char *buffer_find(uint64_t payload, size_t len);
    void buffer_init(size_t mem);

    size_t m_hitsize = 0;

  private:
    std::vector<buffer_t> m_buffers;
    size_t m_current_size = 0;
    size_t m_max_size = 0;
};
