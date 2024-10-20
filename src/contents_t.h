#pragma once


#include "io.hpp"

#include <string>
#include <vector>

class contents_t {
public:
    bool unchanged = false;
    STRING name;
    STRING link;
    uint64_t size;
    uint64_t payload;
    uint32_t checksum;
    time_ms_t file_modified;
    time_ms_t file_c_time; // created on Windows, status change on nix
    int attributes;
    bool directory;
    bool symlink;
    checksum_t ct;
    STRING extra;
    STRING abs_path;
    uint64_t file_id; // diff files refer to this for unchanged files
    bool in_diff;

    bool is_dublicate_of_full = false;
    bool is_dublicate_of_diff = false;
    uint64_t dublicate = 0;

    std::string hash;
    uint64_t first = 0;
    uint64_t last = 0;


};