#include <assert.h>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define fseeko64 _fseeki64
#endif

#include "binding.hpp"

using namespace std;

size_t rd(vector<char>& dst, size_t len, FILE* f, size_t offset, bool exact) {
    dst.resize(len + offset);
    size_t r = fread(dst.data() + offset, 1, len, f);
    assert(!exact || r == len);
    dst.resize(r);
    return r;
}


// hash_size: Set to around 1 MB per 100 MB of input data
// level: 1...3 means LZ compression is done after deduplication, 0 means no LZ
void compress(size_t hash_size, int threads, size_t chunk_size, int level) {
    assert(level >= 0 && level <= 3);

    vector<char> in;
    vector<char> out;
    compressor::init(threads, hash_size, level);

    while (std::cin.peek() != EOF) {
        size_t len = rd(in, chunk_size, stdin, 0, false);
        compressor::compress(in, out);
        fwrite(out.data(), 1, out.size(), stdout);
    }

    compressor::flush(out);
    fwrite(out.data(), 1, out.size(), stdout);
    fflush(stdout); // required
    compressor::uninit();
} 


// Read consecutive packets. A packet can either contain a block of user payload, or it can be
// a reference that into past written data (i.e. we are at a duplicated sequence of data).
void decompress(string outfile) {
    vector<char> in;
    vector<char> out;

    decompressor::init();
    FILE* ofile = fopen(outfile.c_str(), "wb+");

    while (std::cin.peek() != EOF) {
        size_t len = rd(in, decompressor::header, stdin, 0, true);
        size_t packet = decompressor::packet_size(in);
        len = rd(in, packet - decompressor::header, stdin, decompressor::header, true);

        if (decompressor::is_reference(in)) {
            decompressor::Reference r = decompressor::get_reference(in);
            fseeko64(ofile, r.position, SEEK_SET);
            size_t len = rd(out, r.length, ofile, 0, true);
            fseeko64(ofile, 0, SEEK_END);
        }
        else {
            decompressor::decompress_literals(in, out);
        }

        fwrite(out.data(), out.size(), 1, ofile);
    }

    fclose(ofile);
    decompressor::uninit();
}


// Compression: ./demo -c < input_file > compressed_file
// Decompression: ./demo -d output_file < compressed_file
int main(int argc, char *argv[]) {
#ifdef _WIN32
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
   
    if(argc == 3 && std::string(argv[1]) == "-d") {
        decompress(argv[2]);
    }
    else if(argc == 2 && std::string(argv[1]) == "-c") {
        compress(128 * 1024 * 1024ull, (rand() % 20) + 1, 1024 * 1024ull, 1);
    }
    else {
        cerr << "demo -c < input_file > compressed_file"
            "\ndemo -d output_file < compressed_file"
            "\n\nnote: decompression to stdout not possible\n";
    }
    return 0;
}