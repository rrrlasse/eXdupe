#pragma once

#include <string>
#include <vector>
#include <utility>

#include "utilities.hpp"

#ifndef _WIN32

bool get_xattr(const std::string &path, std::string &result, const std::string &pattern, bool follow);
int set_xattr(const std::string &path, const std::string &pattern, const std::string &serialized, std::string &fails);

#else

bool get_acl(const STRING &path, std::string &result, bool follow_symlinks);
bool set_acl(const STRING &path, const std::string &data);
void set_privilege(const std::vector<std::wstring> &priv, bool enable);

#endif
