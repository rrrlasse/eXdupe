#pragma once

#include "utilities.hpp"
#include "io.hpp"

#include <string>
#include <vector>

class contents_t {
public:
    bool unchanged = false;
    STRING name;
    STRING link;
    uint64_t size = 0;
    uint64_t payload = 0;
    uint32_t checksum = 0;
    time_ms_t file_modified = 0;
    time_ms_t file_c_time = 0; // created on Windows, status change on nix
    int attributes = 0;
    bool directory = false;
    bool symlink = false;
    checksum_t ct{};
    STRING extra;
    STRING abs_path;
    uint64_t file_id = 0; // diff files refer to this for unchanged files
    bool in_diff = false;
    bool is_duplicate_of_full = false;
    bool is_duplicate_of_diff = false;
    uint64_t duplicate = 0;
    std::string hash;
    uint64_t first = 0;
    uint64_t last = 0;
};