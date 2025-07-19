#pragma once

#include <iostream>
#include <cstdlib>
#include <source_location>
#include <string>
#include <type_traits>
#include <stdarg.h>
#include <mutex>

#include "unicode.h"
#ifdef _WIN32
#include "shadow/shadow.h"
#endif


enum class retvals {err_other = 1, err_parameters = 2, err_memory = 3, err_write = 4, err_assert = 5, err_permission = 6, err_std_etc = 7, err_corrupted = 8};

inline void cleanup_and_exit([[maybe_unused]] retvals ret) {
    throw ret;
}

template<typename T> inline void print_argument(const T& arg) {
    if constexpr(std::same_as<T, std::wstring>) {
        CERR << arg.c_str() << std::endl;
    }
    else {
        CERR << arg << std::endl;
    }
}

template<typename... Args> inline void rassert_function(const char* condition, const char* message, const std::source_location& location = std::source_location::current(), Args&&... args) {
    std::string f = location.file_name();
    size_t pos = f.find_last_of(PATHDELIMS);
    f = (pos != std::string::npos) ? f.substr(pos + 1) : f;

    CERR << std::endl << (message ? message : "Assert failed!") << std::endl
        << "Condition: " << condition << std::endl
        << "Source: " << f.c_str() << ":" << location.line() << std::endl
        << "Function: " << location.function_name() << std::endl;

    if constexpr (sizeof...(args) > 0) {
        CERR << "Extra information:" << std::endl;
        (print_argument(args), ...);
    }
    cleanup_and_exit(retvals::err_assert);
}

#define massert(condition, message, ...) \
    if (!(condition)) { \
        rassert_function(#condition, message, std::source_location::current(), __VA_ARGS__); \
    }

#define rassert(condition, ...) \
    if (!(condition)) { \
        rassert_function(#condition, nullptr, std::source_location::current(), ##__VA_ARGS__); \
    }