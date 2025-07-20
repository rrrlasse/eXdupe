#pragma once

#include "utilities.hpp"
#include "io.hpp"

#include <string>
#include <vector>

class contents_t {
public:
    STRING name;
    STRING link;
    uint64_t size = 0;
    uint64_t payload = 0;
    time_ms_t file_modified = 0;
    time_ms_t file_c_time = 0; // created on Windows, status change on nix
    int attributes = 0;
    bool directory = false;
    bool symlink = false;
    STRING extra;
    STRING extra2;
    STRING abs_path;
    uint64_t file_id = 0; // diff files refer to this for unchanged files
    uint64_t duplicate = 0;
    std::array<char, 16> hash{};
    uint64_t first = 0;
    uint8_t last = 0;
#ifdef _WIN32
    bool windows = true;
#else
    bool windows = false;
#endif

};