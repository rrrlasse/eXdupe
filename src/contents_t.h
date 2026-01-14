// SPDX-License-Identifier: MIT
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2025: Lasse Mikkel Reinhold

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
    time_ms_t file_c_time = 0;
    time_ms_t file_change_time = 0;
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
    std::string xattr_acl; // ACL+ADS on Windows, xattr on *nix
    bool sparse = false;

    uint64_t volume = 0;
    uint64_t inode = 0;
    bool is_hardlink = false;

#ifdef _WIN32
    bool windows = true;
#else
    bool windows = false;
#endif

};