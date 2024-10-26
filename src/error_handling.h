#pragma once

#include <iostream>
#include <cstdlib>
#include <source_location>
#include <string>
#include <type_traits>
#include <stdarg.h>

#include "unicode.h"
#ifdef _WIN32
#include "shadow/shadow.h"
#endif



enum retvals {err_other = 1, err_parameters = 2, err_resources = 3, err_nofiles = 4, err_assert = 5};

[[noreturn]] inline void cleanup_and_exit(int ret) {
#ifdef _WIN32
    unshadow();
#endif
    exit(ret);
}

template<typename T> inline void print_argument(const T& arg) {
    if constexpr(std::same_as<T, std::wstring>) {
        CERR << arg.c_str() << std::endl;
    }
    else {
        CERR << arg << std::endl;
    }
}

template<typename... Args> [[noreturn]] inline void rassert_function(const char* condition, const std::source_location& location = std::source_location::current(), Args&&... args) {
    std::string f = location.file_name();
    size_t pos = f.find_last_of("/\\");
    f = (pos != std::string::npos) ? f.substr(pos + 1) : f;

    CERR << "\nAssert failed!" << std::endl
        << "Condition: " << condition << std::endl
        << "Source: " << f.c_str() << ":" << location.line() << std::endl
        << "Function: " << location.function_name() << std::endl;

    if constexpr (sizeof...(args) > 0) {
        CERR << "Extra information:" << std::endl;
        (print_argument(args), ...);
    }
    cleanup_and_exit(err_assert);
}

#define rassert(condition, ...) \
    if (!(condition)) { \
        rassert_function(#condition, std::source_location::current(), __VA_ARGS__); \
    }

#ifdef _WIN32
inline void abort(bool b, int ret, const std::wstring& s) {
    if (b) {
        CERR << std::endl << s << std::endl;
        cleanup_and_exit(ret);
    }
}
#endif

inline void abort(bool b, int ret, const std::string& s) {
    if (b) {
        CERR << std::endl << STRING(s.begin(), s.end()) << std::endl;
        cleanup_and_exit(ret);
    }
}

// todo, legacy
inline void abort(bool b, const CHR* fmt, ...) {
    if (b) {
        CERR << std::endl;
        va_list argv;
        va_start(argv, fmt);
        VFPRINTF(stderr, fmt, argv);
        va_end(argv);
        CERR << std::endl;
        cleanup_and_exit(err_other);
    }
}
