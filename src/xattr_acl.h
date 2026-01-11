#pragma once

#include <string>
#include <vector>
#include <utility>

#include "utilities.hpp"

#ifndef _WIN32

bool get_xattr(const std::string &path, std::string &result, const std::string &pattern, bool follow);
int set_xattr(const std::string &path, const std::string &pattern, const std::string &serialized, std::string &fails);

#else

bool get_property(const std::wstring &path, std::string &result, std::vector<int> streams, bool follow_symlinks);
bool set_property(const STRING &path, const std::string &data);
void set_privilege(const std::vector<std::wstring> &priv, bool enable);
bool has_ads(const std::wstring &path);
#endif
