// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#ifndef UNICODE_H
#define UNICODE_H

#include <string>
using std::string;

#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64)
#define WINDOWS2
#endif

#ifdef WINDOWS2

#define SPRINTF swprintf
#define VSPRINTF vswprintf
#define VFPRINTF vfwprintf
#define FPRINTF fwprintf
#define UNITXT(s) TEXT(s)
#define STRING wstring
#define CHR wchar_t
#define REMOVE _wremove
#define FOPEN _wfopen
#define STRLEN wcslen
#define OSTREAM std::wostream
#define CERR std::wcerr
#else
#define SPRINTF sprintf
#define VSPRINTF vsprintf
#define VFPRINTF vfprintf
#define FPRINTF fprintf
#define UNITXT(s) s
#define STRING string
#define CHR char
#define REMOVE remove
#define FOPEN fopen
#define STRLEN strlen
#define OSTREAM std::ostream
#define CERR std::cerr
#endif

#endif
