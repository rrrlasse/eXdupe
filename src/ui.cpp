// SPDX-License-Identifier: GPL-2.0-or-later
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include <assert.h>
#include "ui.hpp"


#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {
int GetHorizontalCursorPosition() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi);
    return csbi.dwCursorPosition.X;
#else
    FILE* pfile = popen("tput cols", "r");
    if (pfile) {
        int col;
        if (fscanf(pfile, "%d", &col) == 1) {
            pclose(pfile);
            return col;
        }
        pclose(pfile);
    }
    return 0;
#endif
}
}

Statusbar::Statusbar(OSTREAM &os) : m_os(os), m_tmp(10000, CHR('c')) {}

void Statusbar::clear_line() {
    int cursor = GetHorizontalCursorPosition();
    if(cursor < m_term_width) {
        cursor = m_term_width;
    }
    STRING blank_line(cursor, ' ');
    m_os << L("\r") << blank_line << L("\r"); 
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

        if (can_resolve && path != L("-stdin") && path != L("-stdout")) {
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
            line = s2w(suffix(read)) + L("B, ") + s2w(suffix(written)) + L("B, ");
        } else {
            line = s2w(suffix(written)) + L("B, ");
        }

        if (!v3) {
            maxpath = m_term_width - line.size();
        }

        if (m_verbose_level > 0) {
            if (v3 && m_lastpath != path) {
                m_lastpath = path;
                line += path;
                m_os << (is_message ? L("") : L("  ")) << path << L("\n");
            } else if (!v3) {
                clear_line();
                if (path.size() > maxpath) {
                    // Some characters and symbols span 2 columns even on monospace fonts, so this may overshoot maxpath.
                    // wcwidth() apparently rarely works on Linux (returns -1 for Korean and many symbols) and can't be used either.
                    // GetStringTypeW() works perfectly but is Windows only. So we'll just accept the issue.
                    path = path.substr(0, maxpath - 2) + L("..");
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
        m_os << s << L("\n");
    }
}

void Statusbar::print_no_lf(int verbosity, const CHR *fmt, ...) {
    if (verbosity <= m_verbose_level) {
        va_list argv;
        va_start(argv, fmt);
        int printed = VSPRINTF(m_tmp.data(), m_tmp.size(), fmt, argv);
        static_cast<void>(printed);
        assert(printed < m_tmp.size() - 1);
        STRING s = STRING(m_tmp.data());
        va_end(argv);
        m_os << s;
    }
}
