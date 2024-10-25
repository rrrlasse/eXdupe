module;

#include "contents_t.h"
#include "io.hpp"

#include <unordered_map>
#include <vector>

export module IdenticalFiles;

export class IdenticalFiles {
public:
    IdenticalFiles() {
        buf.resize(1024*1024);
    }

    void add(contents_t c) {
        // todo, maybe it should be caller's job to decide what to accept
        if (c.size > 4096 && !c.dublicate && !c.unchanged && !c.directory) {
            all_file_hashes[c.first] = c;
        }
    }

    contents_t identical_to(FILE* ifile, contents_t& file_meta, Cio& io, void (*func)(uint64_t read, STRING file), STRING file) {
        checksum_t c;

        io.seek(ifile, 0, SEEK_SET);
        io.read(buf.data(), 1024, ifile, false);
        checksum_init(&c);
        checksum(buf.data(), 1024, &c);
        file_meta.first = c.result64();

        io.seek(ifile, -1024, SEEK_END);
        io.read(buf.data(), 1024, ifile, false);
        checksum_init(&c);
        checksum(buf.data(), 1024, &c);
        file_meta.last = c.result64();

        auto it = all_file_hashes.find(file_meta.first);
        if (it != all_file_hashes.end()) {
            auto cont = it->second;

            if (cont.size == file_meta.size && cont.first == file_meta.first && cont.last == file_meta.last) {
                io.seek(ifile, 0, SEEK_SET);
                checksum_init(&c);
                for (size_t r; (r = io.read(buf.data(), buf.size(), ifile, false));) {
                    func(r, file);
                    checksum((char*)buf.data(), r, &c);
                }

                auto crc = c.result();
                if (crc == cont.hash) {
                    return cont;
                }
            }
        }

        return {};
    }

private:
    std::unordered_map<uint64_t, contents_t> all_file_hashes;
    std::vector<char> buf;
};