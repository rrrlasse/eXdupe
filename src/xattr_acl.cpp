#include "xattr_acl.h"

#include <cstring>
#include <filesystem>
#include <ranges>

#ifdef _WIN32
#include <Winnt.h>
#include <aclapi.h>
#include <sddl.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/xattr.h>
#include <sys/stat.h>
#include <errno.h>
#include <cstring>
#include <iomanip>
#include <regex>
#include <sstream>
#include <errno.h>

bool list_xattrs_matching(const std::string &path, const std::string &pattern, std::vector<std::pair<std::string, std::vector<char>>> &out, bool followLinks) {
    out.clear();
    std::regex re;
    const char *name = nullptr;

    try {
        re = std::regex(pattern);
    } catch (const std::exception &) {
        return false;
    }

    auto list = [&](char *dst, size_t len) { return followLinks ? listxattr(path.c_str(), dst, len) : llistxattr(path.c_str(), dst, len); };
    auto get = [&](char *dst, size_t len) { return followLinks ? getxattr(path.c_str(), name, dst, len) : lgetxattr(path.c_str(), name, dst, len); };

    ssize_t list_len = list(nullptr, 0);

    if (list_len < 0) {
        if (errno == ENOTTY || errno == ENOTSUP) {
            return true; // supported but no attributes / not supported on FS
        }
        return false;
    }

    if (list_len == 0) {
        return true;
    }

    std::vector<char> names(static_cast<size_t>(list_len) + 1);
    list_len = list(names.data(), names.size());
    
    if (list_len < 0) {
        if (errno == ENOTTY || errno == ENOTSUP) {
            return true; // supported but no attributes / not supported on FS
        }
        return false;
    }

    size_t pos = 0;
    while (pos < static_cast<size_t>(list_len)) {
        name = names.data() + pos;
        size_t name_len = std::strlen(name);

        bool matched = false;
        try {
            if (std::regex_search(std::string(name, name_len), re)) {
                matched = true;
            }
        } catch (...) {
            matched = false;
        }

        if (matched) {
            ssize_t val_len = get(nullptr, 0);

            if (val_len < 0) {
                // skip unreadable attribute
                pos += name_len + 1;
                continue;
            }

            std::vector<char> val;
            if (val_len > 0) {
                val.resize(static_cast<size_t>(val_len));
                ssize_t got = get(val.data(), val.size());
                if (got < 0) {
                    pos += name_len + 1;
                    continue;
                }
                if (static_cast<size_t>(got) != val.size()) {
                    val.resize(static_cast<size_t>(got));
                }
            }
            out.emplace_back(std::string(name, name_len), std::move(val));
        }
        pos += name_len + 1;
    }
    return true;
}

bool get_xattr(const std::string &path, std::string& result, const std::string &pattern, bool follow) {
    std::vector<std::pair<std::string, std::vector<char>>> entries;
    result.clear();

    if (!list_xattrs_matching(path, pattern, entries, follow)) {
        return false;
    }

    std::string out;

    for (const auto &e : entries) {
        const std::string &name = e.first;
        const auto &val = e.second;
        out.append(name);
        out.push_back('\t');
        out.append(std::to_string(val.size()));
        out.push_back('\t');
        if (!val.empty()) {
            out.append(val.data(), val.size());
        }
        out.push_back('\n'); // separator between entries
    }

    result = out;
    return true;
}

// 0 = all set ok; 1 = some not set, see 'fails'; 2 = archive corrupted; 3 = assert
int set_xattr(const std::string &path, const std::string &pattern, const std::string &serialized, std::string& fails) {
    // compile regex
    std::regex re;
    try {
        re = std::regex(pattern);
    } catch (const std::exception &) {
        return 3;
    }

    // detect symlink -> use lsetxattr when setting (must never follow)
    struct stat st;
    bool isSymlink = false;
    if (lstat(path.c_str(), &st) == 0) {
        isSymlink = S_ISLNK(st.st_mode);
    }

    // Parse the new binary serialization:
    // loop: read <name>\t<len>\t<raw-bytes> \n
    size_t pos = 0;
    bool all_ok = true;
    const std::string &s = serialized;
    const size_t N = s.size();

    while (pos < N) {
        // read name up to '\t'
        size_t tab1 = s.find('\t', pos);
        if (tab1 == std::string::npos) {
            // corrupted
            return 2;
        }
        std::string name = s.substr(pos, tab1 - pos);
        pos = tab1 + 1;

        // read length up to next '\t'
        size_t tab2 = s.find('\t', pos);
        if (tab2 == std::string::npos) {
            return 2;
        }
        std::string lenstr = s.substr(pos, tab2 - pos);
        pos = tab2 + 1;

        // parse length
        size_t val_len = 0;
        try {
            val_len = static_cast<size_t>(std::stoull(lenstr));
        } catch (...) {
            return 2;
        }

        if (pos + val_len > N) {
            // truncated
            return 2;
        }

        std::vector<char> val;
        if (val_len > 0) {
            val.assign(s.data() + pos, s.data() + pos + val_len);
        }
        pos += val_len;

        // Expect separator '\n' after the value if there are more bytes
        if (pos < N) {
            if (s[pos] == '\n') {
                pos += 1;
            } else {
                // corrupted format
                return 2;
            }
        }

        // only apply if name matches regex
        bool matched = false;
        try {
            if (std::regex_search(name, re))
                matched = true;
        } catch (...) {
            matched = false;
        }
        if (!matched)
            continue;

        // apply attribute
        int res = -1;
        if (isSymlink) {
            // MUST NOT follow symlinks. If platform provides lsetxattr, use it.
            res = lsetxattr(path.c_str(), name.c_str(), val.empty() ? nullptr : val.data(), val.size(), 0);
        } else {
            // Path is not a symlink: set attribute on the target object directly.
            res = setxattr(path.c_str(), name.c_str(), val.empty() ? nullptr : val.data(), val.size(), 0);
        }

        if (res != 0) {
            all_ok = false;
            fails += name + " ";
        }
    }

    return all_ok ? 0 : 1;
}

#endif

#ifdef _WIN32

bool get_property(const std::wstring &path, std::string &result, std::vector<int> streams, bool follow_symlinks) {
    result.clear();
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (!follow_symlinks)
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ | READ_CONTROL | ACCESS_SYSTEM_SECURITY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, flags, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LPVOID context = NULL;
    WIN32_STREAM_ID sid;
    DWORD bytesRead = 0;
    const DWORD sidHeaderSize = offsetof(WIN32_STREAM_ID, cStreamName);

    while (true) {
        if (!BackupRead(hFile, (LPBYTE)&sid, sidHeaderSize, &bytesRead, FALSE, TRUE, &context) || bytesRead == 0)
            break;

        std::vector<char> nameBuffer;
        if (sid.dwStreamNameSize > 0) {
            nameBuffer.resize(sid.dwStreamNameSize);
            BackupRead(hFile, (LPBYTE)nameBuffer.data(), sid.dwStreamNameSize, &bytesRead, FALSE, TRUE, &context);
        }

        if (std::ranges::any_of(streams, [&](int s) { return (DWORD)s == sid.dwStreamId; })) {
            result.append(reinterpret_cast<const char *>(&sid), sidHeaderSize);
            if (!nameBuffer.empty()) {
                result.append(nameBuffer.data(), nameBuffer.size());
            }

            if (sid.Size.QuadPart > 0) {
                std::vector<char> dataBuffer(sid.Size.LowPart);
                if (BackupRead(hFile, (LPBYTE)dataBuffer.data(), sid.Size.LowPart, &bytesRead, FALSE, TRUE, &context)) {
                    result.append(dataBuffer.data(), bytesRead);
                }
            }
        } else {
            DWORD lowSeek = 0, highSeek = 0;
            BackupSeek(hFile, sid.Size.LowPart, sid.Size.HighPart, &lowSeek, &highSeek, &context);
        }
    }

    BackupRead(hFile, NULL, 0, &bytesRead, TRUE, FALSE, &context);
    CloseHandle(hFile);
    return true;
}

// Never follows symlinks
bool set_property(const std::wstring &path, const std::string &data, std::vector<int> streams) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE | WRITE_OWNER | WRITE_DAC | ACCESS_SYSTEM_SECURITY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LPVOID context = NULL;
    DWORD bytesWritten = 0;
    const DWORD sidHeaderSize = offsetof(WIN32_STREAM_ID, cStreamName);

    size_t offset = 0;
    bool overallSuccess = true;

    while (offset + sidHeaderSize <= data.size()) {
        // 1. Læs header fra bufferen
        WIN32_STREAM_ID *sid = (WIN32_STREAM_ID *)&data[offset];
        size_t totalBlockSize = sidHeaderSize + sid->dwStreamNameSize + (size_t)sid->Size.QuadPart;

        // 2. Tjek om denne stream-type skal restores
        bool shouldRestore = std::ranges::any_of(streams, [&](int s) { return (DWORD)s == sid->dwStreamId; });

        if (shouldRestore) {
            // Skriv hele blokken (Header + Navn + Data) til filen
            if (!BackupWrite(hFile, (LPBYTE)&data[offset], (DWORD)totalBlockSize, &bytesWritten, FALSE, TRUE, &context)) {
                overallSuccess = false;
                break;
            }
        }

        // Flyt offset til næste stream i bufferen
        offset += totalBlockSize;
    }

    // Ryd op i context (Vigtigt: bAbort = TRUE hvis vi fejlede undervejs)
    DWORD dummy;
    BackupWrite(hFile, NULL, 0, &dummy, overallSuccess ? FALSE : TRUE, FALSE, &context);

    CloseHandle(hFile);
    return overallSuccess;
}



// intentional soft failure; if this fails, just attempt to set/get acls anyway
void set_privilege(const std::vector<std::wstring> &priv, bool enable) {
    for (auto &p : priv) {
        HANDLE hToken;
        TOKEN_PRIVILEGES tp;
        LUID luid;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
            continue;
        if (!LookupPrivilegeValueW(NULL, p.c_str(), &luid))
            continue;

        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;

        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        CloseHandle(hToken);
    }
}

bool has_ads(const std::wstring &path) {
    WIN32_FIND_STREAM_DATA streamData;
    HANDLE a = FindFirstStreamW(path.c_str(), FindStreamInfoStandard, &streamData, 0);
    if (a) {
        FindClose(a);
        return true;
    }
    return false;
}
#endif
