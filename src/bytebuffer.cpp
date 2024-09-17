// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include <vector>

#include "bytebuffer.h"
#include "utilities.hpp"

Bytebuffer::Bytebuffer(size_t max_size) {
    m_buffer.resize(max_size);
}

void Bytebuffer::buffer_add(const unsigned char *src, uint64_t payload, size_t len) {
    char *insert_at = 0;

    if (m_buffers.size() == 0) {
        insert_at = m_buffer.data();
    } 
    else if (m_buffers.back().buffer_offset - m_buffer.data() + m_buffers.back().len + len <= m_buffer.size()) {
        insert_at = m_buffers.back().buffer_offset + m_buffers.back().len;
        unsigned int del = 0;
        while (m_buffers.size() > 0 && m_buffers.size() > del && m_buffers[del].buffer_offset < insert_at + len && m_buffers[del].buffer_offset >= insert_at) {
            del++;
        }
        if (del > 0) {
            m_buffers.erase(m_buffers.begin(), m_buffers.begin() + del);
        }
    } 
    else if (m_buffers.back().buffer_offset - m_buffer.data() + m_buffers.back().len + len > m_buffer.size()) {
        insert_at = m_buffer.data();
        int del = 0;
        while (m_buffers[del].buffer_offset != m_buffer.data()) {
            del++;
        }
        if (del > 0) {
            m_buffers.erase(m_buffers.begin(), m_buffers.begin() + del);
        }

        del = 0;
        while (m_buffers[del].buffer_offset < insert_at + len + 1024 * 1024) {
            del++;
        }

        if (del > 0) {
            m_buffers.erase(m_buffers.begin(), m_buffers.begin() + del);
        }
    }
    abort(insert_at == 0, UNITXT("insert_at == 0"));

    buffer_t b;
    b.pay = payload;
    b.buffer_offset = insert_at;
    memcpy(insert_at, src, len);
    b.len = len;
    m_buffers.push_back(b);
}

char *Bytebuffer::buffer_find(uint64_t payload, size_t len) {
    for (unsigned int i = 0; i < m_buffers.size(); i++) {
        if (payload >= m_buffers[i].pay && payload + len <= m_buffers[i].pay + m_buffers[i].len) {
            size_t off = 0;
            if (payload > m_buffers[i].pay) {
                off = payload - m_buffers[i].pay;
            }
            return m_buffers[i].buffer_offset + off;
        }
    }
    return 0;
}