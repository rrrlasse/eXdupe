// SPDX-License-Identifier: MIT
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2025: Lasse Mikkel Reinhold

#include <cstdint>
#include <stdio.h>
#include <vector>

#include "gsl/narrow"

#include "io.hpp"
#include "unicode.h"
#include "utilities.hpp"
#include "abort.h"
#include "aes.hpp"
#include <optional>
#include <unordered_map>
#include <array>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#if defined(hpux) || defined(__hpux) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__FreeBSD__)
#define _ftelli64 ftello
#define _fseeki64 fseeko
#elif defined(__APPLE__)
#define _ftelli64 ftell
#define _fseeki64 fseek
#else
#define _ftelli64 ftello64
#define _fseeki64 fseeko64
#endif
#endif

using std::wstring;

void Cio::set_encryption(const std::string &passphrase, const std::string &iv, const std::string &passphrase_salt) {    
    m_passphrase = passphrase;
    m_iv = iv;
    m_passphrase_salt = passphrase_salt;
    m_derived_key = dup_crypto::derive_key_from_passphrase(m_passphrase, m_passphrase_salt);
    m_passphrase_salt.clear();
    m_passphrase.clear();
}

void Cio::disable_encryption() {
    m_passphrase.clear();
    m_iv.clear();
    m_passphrase_salt.clear();
    m_derived_key.reset();
}

Cio::Cio() {
    write_count = 0;
    read_count = 0;
}

// Only way I could find that detected both pipes and redirection. Todo, is this OK?
bool Cio::stdin_tty() {
#ifdef _WIN32
    return _isatty(0);
#else
    return isatty(0);
#endif
}

int Cio::close(FILE *_File, bool sparse) {
    if (sparse) {
        truncate(_File);
    }
    return fclose(_File); 
}

FILE *Cio::open(STRING file, char mode) {
    file = lp(file);
    if (mode == 'r') {
        STRING s = file;
        FILE *f = FOPEN(s.c_str(), L("rb"));
        return f;
    } else if (mode == 'w') {
        return FOPEN(file.c_str(), L("wb+"));
    } else if (mode == 'a') {
        return FOPEN(file.c_str(), L("r+b"));    
    } else {
        rassert(false);
    }

}

uint64_t Cio::tell(FILE *_File) {
    if (_File == stdout) {
        return write_count;
    } else if (_File == stdin) {
        return read_count;
    }
    return _ftelli64(_File); 

}

int Cio::seek(FILE *_File, int64_t _Offset, int Origin) { return _fseeki64(_File, _Offset, Origin); }

size_t Cio::write(const void *Str, size_t Count, FILE *_File, bool sparse) {
    const void *writebuf;
    std::unique_ptr<uint8_t[]> rawbuf;

    if (!m_derived_key.has_value()) {
        writebuf = Str;
    }
    else {
        auto &key = m_derived_key.value();
        long long filepos = tell(_File);
        uint64_t data_offset = static_cast<uint64_t>(filepos);

        if (Count > 0) {
            if (m_scratch_buffer.size() < Count)
                m_scratch_buffer.resize(Count);
            std::memcpy(m_scratch_buffer.data(), Str, Count);
        }

        // Use centralized AES CTR helper to handle IV and unaligned offsets
        dup_crypto::aes256_ctr_xor_with_iv(m_scratch_buffer.data(), Count, key.data(), reinterpret_cast<const uint8_t*>(m_iv.data()), static_cast<uint64_t>(data_offset));

        writebuf = m_scratch_buffer.data();
    }

    size_t c = 0;
    if (!sparse) {
        while (c < Count) {
            size_t w = Count - c;
            size_t r = fwrite((char *)writebuf + c, 1, w, _File);
            write_count += r;
            abort(r != w, retvals::err_write, "Disk full or write denied while writing destination file");
            c += r;
        }
        return Count;
    }

    while (c < Count) {
        size_t run_start = c;
        const char *ptr = static_cast<const char *>(writebuf);
        while (c < Count && ptr[c] == 0) {
            c++;
        }
        size_t zero_run_len = c - run_start;
        if (zero_run_len > 0) {
            seek(_File, static_cast<off_t>(zero_run_len), SEEK_CUR);
            if (c == Count)
                break;
        }
        run_start = c;
        while (c < Count && ptr[c] != 0) {
            c++;
        }
        size_t data_run_len = c - run_start;
        if (data_run_len > 0) {
            size_t r = std::fwrite(ptr + run_start, 1, data_run_len, _File);
            if (r != data_run_len) {
                abort(true, retvals::err_write, L("Write failed")); // FIXME show filename to user
            }
            write_count += r;
        }
    }
    return Count;
    

}

size_t Cio::read(void* DstBuf, size_t Count, FILE* _File, bool read_exact) {
    size_t actually_read = 0;

    for(;;) {
        size_t r = Count - actually_read;
        size_t w = fread((char*)DstBuf + actually_read, 1, r, _File);
        read_count += w;
        abort(read_exact && stdin_tty() && w != r, L("Unexpected end of source file"));
        actually_read += w;
        if(actually_read == Count || w != r) {
            break;
        }
    }
    if (!m_derived_key.has_value()) {
        return actually_read;
    }

    long long data_offset = tell(_File) - actually_read;
    abort(m_iv.size() != ENC_IV_LEN, retvals::err_other, L("Invalid IV size for decryption"));

    const uint8_t *iv12 = reinterpret_cast<const uint8_t*>(m_iv.data());
    if (actually_read > 0) {
        dup_crypto::aes256_ctr_xor_with_iv(static_cast<uint8_t*>(DstBuf), actually_read, m_derived_key->data(), iv12, static_cast<uint64_t>(data_offset));
    }

    return actually_read;
}

size_t Cio::read_vector(std::vector<char>& dst, size_t count, size_t offset, FILE* f, bool read_exact) {
    if(dst.size() < count + offset) {
        dst.resize(count + offset);
    }
    return read(dst.data() + offset, count, f, read_exact);
}


std::string Cio::read_bin_string(size_t Count, FILE *_File) {
    std::string str(Count, 'c');
    if(Count > 0) {
        size_t r = Cio::read(&str[0], Count, _File, true);
        abort(stdin_tty() && r != Count, L("Unexpected end of source file"));
    }
    return str;
}

STRING Cio::read_utf8_string(FILE *_File) {
    uint64_t t = read_compact<uint64_t>(_File);
    std::string tmp = read_bin_string(t, _File);
#ifdef _WIN32
    int req = MultiByteToWideChar(CP_UTF8, 0, tmp.c_str(), -1, nullptr, 0);
    wstring res(req, 'c');
    MultiByteToWideChar(CP_UTF8, 0, tmp.c_str(), -1, &res[0], gsl::narrow<int>(t));
    res.pop_back(); // WideCharToMultiByte() adds trailing zero
    return res;
#else
    return tmp;
#endif
}

void Cio::write_utf8_string(STRING str, FILE *_File) {
    // Note: overload with optional key exists in header; this implementation keeps legacy behavior
#ifdef _WIN32
    int req = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> v(req, L'c');
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &v[0], static_cast<int>(req), 0, 0);
    req--; // WideCharToMultiByte() adds trailing zero
    write_compact<uint64_t>(req, _File);
    write(&v[0], req, _File, false);
#else
    write_compact<uint64_t>(str.size(), _File);
    write(str.c_str(), str.size(), _File, false);
#endif
    }

void Cio::truncate(FILE *file) {
#ifdef _WIN32
        int fd = _fileno(file);
        HANDLE hFile = (HANDLE)_get_osfhandle(fd);
        int e = SetEndOfFile(hFile);
        int er = GetLastError();
        rassert(e);
#else
        long pos = ftell(file);
        int fd = fileno(file);
        rassert(ftruncate(fd, pos) == 0);
#endif
    }