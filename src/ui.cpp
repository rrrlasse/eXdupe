// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include "ui.hpp"

Statusbar::Statusbar(OSTREAM &os) : m_os(os) {}

void Statusbar::clear_line() {
    STRING blank_line(m_term_width, ' ');
    m_os << UNITXT("\r") << blank_line << UNITXT("\r"); 
};

void Statusbar::update(status_t status, uint64_t read, uint64_t written, STRING path, bool no_delay) {
    if (m_verbose_level < 1) {
        return;
    }

    bool backup = status == BACKUP || status == DIFF_BACKUP;
    bool v3 = m_verbose_level == 3;
    size_t maxpath = size_t(-1);

    bool can_resolve = abs_path(path).size() > 0;

    if (can_resolve && path != UNITXT("-stdin") && path != UNITXT("-stdout")) {
        path = abs_path(path);
    }

    if (!v3 && can_resolve) {
        path = path.substr(m_base_dir.size());
        path = remove_delimitor(path);
        path = remove_leading_delimitor(path);
    }

    uint64_t f = GetTickCount() - m_last_file_print;

    if ((no_delay || f >= 1000) || v3) {
        m_last_file_print = GetTickCount();
        STRING line;
        if (backup) {
            line = s2w(format_size(read)) + UNITXT(", ") + s2w(format_size(written)) + UNITXT(", ");
        } else {
            line = s2w(format_size(written)) + UNITXT(", ");
        }

        if (!v3) {
            maxpath = m_term_width - line.size();
        }

        if (m_verbose_level > 0) {
            if (v3 && m_lastpath != path) {
                m_lastpath = path;
                line += path;
                m_os << UNITXT("  ") << path << UNITXT("\n");
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


void Statusbar::print(int verbosity, const CHR *fmt, ...) {
    if (verbosity > m_verbose_level) {
        return;
    }
    va_list argv;
    va_start(argv, fmt);
    VSPRINTF(m_wtmp, fmt, argv);
    STRING s = STRING(m_wtmp);
    va_end(argv);
    clear_line();
    m_os << s << UNITXT("\n");
}
