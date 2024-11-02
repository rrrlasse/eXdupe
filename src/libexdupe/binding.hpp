#include <assert.h>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "libexdupe.h"

namespace compressor {
    namespace detail {
        std::vector<char> hash_table;
        std::vector<std::vector<char>> in;
        std::vector<std::vector<char>> out;
        size_t ptr = 0;
        int threads;
    }

    void init(int threads, size_t hash_size, int level) {
        assert(threads >= 1);
        assert(hash_size >= 1024 * 1024);
        assert(level >= 0 && level <= 3);
        
        using namespace detail;
        detail::threads = threads;
        hash_table.resize(hash_size);
        in.resize(threads + 10);
        out.resize(threads + 10);
        dup_init_compression(128 * 1024, 4 * 1024, hash_size, threads, hash_table.data(), level, false, 0, 0);
    }

    void uninit() {
        dup_uninit_compression();
    }

    uint64_t prev = 0;
    void compress(const std::vector<char>& src, std::vector<char>& dst) {
        using namespace detail;        
        char* compressed;
        std::vector<char> vec;
        in[ptr] = src;
        out[ptr].resize(src.size() + 128 * 1024);
        size_t ret = dup_compress(in[ptr].data(), out[ptr].data(), src.size(), 0, false, &compressed, 0);
        ptr = (ptr + 1) % in.size();
        dst = { compressed, compressed + ret };
    }

    void flush(std::vector<char>& dst) {
        using namespace detail;
        char* res;
        size_t len;

        dst.clear();
        do {
            len = dup_flush_pend_block(0, &res, 0);
            dst.insert(dst.end(), res, res + len);
        } while (len > 0);
    }
}

namespace decompressor {
    size_t header = DUP_HEADER_LEN;

    struct Reference {
        size_t length;
        size_t position;
    };

    void init() {
        dup_init_decompression();
    }

    void uninit() {
        dup_uninit_decompression();
    }

    bool is_reference(const std::vector<char>& src) {
        int r = dup_packet_info(src.data(), 0, 0);
        assert(r == DUP_REFERENCE || r == DUP_LITERAL);
        return r == DUP_REFERENCE;
    }

    void decompress_literals(const std::vector<char>& src, std::vector<char>& dst) {
        assert(!is_reference(src));
        uint64_t pay;
        size_t len = dup_size_decompressed(src.data());
        dst.resize(len);
        int r = dup_decompress(src.data(), dst.data(), &len, &pay);
        assert(r == 0 || r == 1);
    }

    size_t packet_size(const std::vector<char>& src) {
        size_t r = dup_size_compressed(src.data());
        assert(r);
        return r;
    }

    Reference get_reference(const std::vector<char>& src) {
        assert(is_reference(src));
        uint64_t pay;
        size_t len;
        int r = dup_packet_info(src.data(), &len, &pay);
        assert(r);
        return Reference(len, pay);
    }
}