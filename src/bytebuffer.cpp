// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2023: Lasse Mikkel Reinhold

#include "bytebuffer.h"
#include "utilities.hpp"
#include <vector>

char *buff;

typedef struct {
    uint64_t pay;
    size_t len;
    char *buffer_offset;
} buffer_t;
std::vector<buffer_t> buffers;

size_t buffer_size;

void buffer_init(size_t mem) {
    buff = (char *)tmalloc(mem);
    buffer_size = mem;
}

void buffer_add(const unsigned char *src, uint64_t payload, size_t len) {
    char *insert_at = 0;

    if (buffers.size() == 0) {
        insert_at = buff;
    } else if (buffers.back().buffer_offset - buff + buffers.back().len + len <= buffer_size) {
        insert_at = buffers.back().buffer_offset + buffers.back().len;
        unsigned int del = 0;
        while (buffers.size() > 0 && buffers.size() > del && buffers[del].buffer_offset < insert_at + len && buffers[del].buffer_offset >= insert_at) {
            del++;
        }
        if (del > 0) {
            buffers.erase(buffers.begin(), buffers.begin() + del);
        }
    } else if (buffers.back().buffer_offset - buff + buffers.back().len + len > buffer_size) {
        insert_at = buff;
        int del = 0;
        while (buffers[del].buffer_offset != buff) {
            del++;
        }
        if (del > 0) {
            buffers.erase(buffers.begin(), buffers.begin() + del);
        }

        del = 0;
        while (buffers[del].buffer_offset < insert_at + len + 4 * 1024 * 1024) {
            del++;
        }

        if (del > 0) {
            buffers.erase(buffers.begin(), buffers.begin() + del);
        }
    }
    abort(insert_at == 0, UNITXT("insert_at == 0"));

    buffer_t b;
    b.pay = payload;
    b.buffer_offset = insert_at;
    memcpy(insert_at, src, len);
    b.len = len;
    buffers.push_back(b);
}

char *buffer_find(uint64_t payload, size_t len) {
    for (unsigned int i = 0; i < buffers.size(); i++) {
        if (payload >= buffers[i].pay && payload + len <= buffers[i].pay + buffers[i].len) {
            size_t off = 0;
            if (payload > buffers[i].pay) {
                off = payload - buffers[i].pay;
            }
            return buffers[i].buffer_offset + off;
        }
    }
    return 0;
}
