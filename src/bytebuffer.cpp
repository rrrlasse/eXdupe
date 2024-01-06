// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include <vector>

#include "bytebuffer.h"
#include "utilities.hpp"

Bytebuffer::Bytebuffer(size_t max_size) : m_max_size(max_size) {}

void Bytebuffer::buffer_add(const unsigned char *src, uint64_t offset, size_t len) {
    // Return if fully contained already. Partial overlap is OK
    if (len > m_max_size || buffer_find(offset, len)) {
        return;
    }

    while (m_current_size + len > m_max_size) {
        m_current_size -= m_buffers.begin()->data.size();
        m_buffers.erase(m_buffers.begin());
    }
    std::vector<unsigned char> v(src, src + len);
    m_buffers.emplace_back(buffer_t(offset, std::move(v)));
    m_current_size += len;
}

char *Bytebuffer::buffer_find(uint64_t offset, size_t len) {
    for (unsigned int i = 0; i < m_buffers.size(); i++) {
        if (offset >= m_buffers[i].offset && offset + len <= m_buffers[i].offset + m_buffers[i].data.size()) {
            size_t off = 0;
            if (offset > m_buffers[i].offset) {
                off = offset - m_buffers[i].offset;
            }
            m_hitsize += len;
            return reinterpret_cast<char *>(m_buffers[i].data.data() + off);
        }
    }
    return nullptr;
}
