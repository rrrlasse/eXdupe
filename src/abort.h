#pragma once

#include "utilities.hpp"

#ifdef _WIN32
void abort(bool b, retvals ret, const std::wstring &s);
#endif

void abort(bool b, retvals ret, const std::string &s);

// todo, legacy
void abort(bool b, const CHR *fmt, ...);