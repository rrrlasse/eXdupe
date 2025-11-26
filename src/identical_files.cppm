#pragma once

#include "contents_t.h"
#include "io.hpp"

#include <unordered_map>
#include <vector>
#include <optional>

class IdenticalFiles {
public:
    void add(contents_t c) {
        if (!c.directory) {
            c.abs_path.clear();
            c.name.clear();
            all_file_hashes[c.first] = c;
        }
    }

    std::optional<contents_t> identical_to(FILE *ifile, contents_t &file_meta, Cio &io, void (*func)(uint64_t read, const STRING &file), STRING file, uint32_t hash_seed, bool use_aesni) {
        checksum_t c;

        io.seek(ifile, 0, SEEK_SET);
        int64_t r = io.read_vector(buf, 1024, 0, ifile, false);
        checksum_init(&c, hash_seed, use_aesni);
        checksum(buf.data(), r, &c);
        file_meta.first = c.result64();

        io.seek(ifile, -r, SEEK_END);
        io.read_vector(buf, r, 0, ifile, false);
        checksum_init(&c, hash_seed, use_aesni);
        checksum(buf.data(), r, &c);
        file_meta.last = static_cast<uint8_t>(c.result64());
        
        auto it = all_file_hashes.find(file_meta.first);
        if (it != all_file_hashes.end()) {
            auto candidate = it->second;
            if (candidate.size == file_meta.size && candidate.first == file_meta.first && candidate.last == file_meta.last) {
                io.seek(ifile, 0, SEEK_SET);
                checksum_init(&c, hash_seed, use_aesni);
                size_t total_read = 0;
                for (size_t r; (r = io.read_vector(buf, 1024 * 1024, 0, ifile, false));) {
                    func(r, file);
                    checksum((char*)buf.data(), r, &c);
                    total_read += r;
                }

                auto crc = c.result();
                if (crc == candidate.hash && candidate.size == total_read) {
                    return candidate;
                }
            }
        }

        return {};
    }

private:
    std::unordered_map<uint64_t, contents_t> all_file_hashes;
    std::vector<char> buf;
    int emulate_avx;
};