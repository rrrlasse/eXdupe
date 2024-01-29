// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include <assert.h>
#include "ui.hpp"

Statusbar::Statusbar(OSTREAM &os) : m_os(os), m_tmp(10000, CHR('c')) {}

void Statusbar::clear_line() {
    STRING blank_line(m_term_width, ' ');
    m_os << UNITXT("\r") << blank_line << UNITXT("\r"); 
};

// If is_message, then treat path as a status message and don't prepend a path to it
void Statusbar::update(status_t status, uint64_t read, uint64_t written, STRING path, bool no_delay, bool is_message) {
    if (m_verbose_level < 1 || path.size() == 0) {
        return;
    }

    bool backup = status == BACKUP || status == DIFF_BACKUP;
    bool v3 = m_verbose_level == 3;
    size_t maxpath = size_t(-1);

    if(!is_message) {
        bool can_resolve = abs_path(path).size() > 0;

        if (can_resolve && path != UNITXT("-stdin") && path != UNITXT("-stdout")) {
            path = abs_path(path);
        }

        if (!v3 && can_resolve) {
            path = path.substr(m_base_dir.size());
            path = remove_delimitor(path);
            path = remove_leading_delimitor(path);
        }
    }
    uint64_t f = GetTickCount() - m_last_file_print;

    if ((no_delay || f >= 1000) || v3) {
        m_last_file_print = GetTickCount();
        STRING line;
        if (backup) {
            line = s2w(format_size(read)) + UNITXT("B, ") + s2w(format_size(written)) + UNITXT("B, ");
        } else {
            line = s2w(format_size(written)) + UNITXT("B, ");
        }

        if (!v3) {
            maxpath = m_term_width - line.size();
        }

        if (m_verbose_level > 0) {
            if (v3 && m_lastpath != path) {
                m_lastpath = path;
                line += path;
                m_os << (is_message ? UNITXT("") : UNITXT("  ")) << path << UNITXT("\n");
            } else if (!v3) {
                clear_line();
                if (path.size() > maxpath) {
                    path = path.substr(0, maxpath - 2) + UNITXT("..");
                }
                line += path;
                m_os << line;
            }
        }
    }
}

// FIXME rewrite to just taking std::string as parameter
void Statusbar::print(int verbosity, const CHR *fmt, ...) {
    if (verbosity <= m_verbose_level) {
        va_list argv;
        va_start(argv, fmt);
        int printed = VSPRINTF(m_tmp.data(), m_tmp.size(), fmt, argv);
        static_cast<void>(printed);
        assert(printed < m_tmp.size() - 1);
        STRING s = STRING(m_tmp.data());
        va_end(argv);
        clear_line();
        m_os << s << UNITXT("\n");
    }
}
