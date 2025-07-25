#pragma once

// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2025: Lasse Mikkel Reinhold

#include <iostream>
#include <optional>
#include <string>
#include <mutex>

#include "utilities.hpp"

class Statusbar {
  public:
    Statusbar(OSTREAM& os = CERR);
    void print(int verbosity, const CHR *fmt, ...);
    void print_no_lf(int verbosity, const CHR *fmt, ...);
    void update(status_t status, uint64_t read, uint64_t written, STRING path, bool no_delay = false, bool is_message = false);
    void print_abort_message(const CHR *fmt, ...);
    void clear_line();

    int m_verbose_level{};
    STRING m_base_dir;
    int m_term_width = 78;

private:
    STRING m_lastpath;
    uint64_t m_last_file_print{};
    OSTREAM& m_os;
    std::vector<CHR> m_tmp;
    std::recursive_mutex screen_mutex;
};
