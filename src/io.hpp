// eXdupe deduplication file archiver and library
//
// Contributers:
//
// 2010 - 2023: Lasse Mikkel Reinhold
//
// eXdupe is now Public Domain (PD): The world's fastest deduplication with the
// worlds least restrictive terms.

#include "unicode.h"

#ifndef AIO_HEADER
#define AIO_HEADER

#include "utilities.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WINDOWS
#include <windows.h>
#endif

using namespace std;

class Cio {
  private:
    char tmp[4096];
    wchar_t wtmp[4096];
    CHR Ctmp[4096];

  public:
    uint64_t read_count;
    uint64_t write_count;

    Cio();
    //	void Cio::ahead(STRING file);
    int close(FILE *_File);
    FILE *open(STRING file, char mode);
    uint64_t tell(FILE *_File);
    int seek(FILE *_File, int64_t _Offset, int Origin);
    bool write_date(struct tm *t, FILE *_File);
    bool read_date(struct tm *t, FILE *_File);
    size_t read(void *_DstBuf, size_t _Count, FILE *_File);
    size_t write(const void *_Str, size_t _Count, FILE *_File);
    size_t try_write(const void *Str, size_t Count, FILE *_File);
    size_t try_read(void *DstBuf, size_t Count, FILE *_File);
    size_t write64(uint64_t i, FILE *_File);
    size_t write32(unsigned int i, FILE *_File);
    size_t write8(char i, FILE *_File);
    char read8(FILE *_File);
    unsigned int read32(FILE *_File);
    uint64_t read64(FILE *_File);
    STRING readstr(FILE *_File);
    size_t writestr(STRING str, FILE *_File);
};
#endif
