#include "attrib.h"

#include <cstring>
#include <filesystem>

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
bool get_acl(const STRING &path, std::string &result, bool follow) {
    // std::cerr << "X\n";
    result.clear();

    // Use absolute path for the security API
    STRING ap = abs_path(path);
    if (ap.empty()) {
        ap = path;
    }

    PSECURITY_DESCRIPTOR sd = nullptr;
    PSID owner = nullptr, group = nullptr;
    PACL dacl = nullptr, sacl = nullptr;

    // DWORD reqFlags = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION;
    DWORD reqFlags = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION | ATTRIBUTE_SECURITY_INFORMATION | SCOPE_SECURITY_INFORMATION | BACKUP_SECURITY_INFORMATION |
                     PROTECTED_DACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION;

    if (std::filesystem::is_symlink(path) && !follow) {
        // When not following symlinks, open the reparse point itself
        HANDLE h = CreateFileW(ap.c_str(), READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD res = GetSecurityInfo(h, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION, &owner, &group, &dacl, &sacl, &sd);
        CloseHandle(h);
        if (res != ERROR_SUCCESS || sd == nullptr) {
            if (sd) {
                LocalFree(sd);
            }
            return false;
        }
    } else {
        // Follow symlinks (default)

        DWORD res = GetNamedSecurityInfoW(const_cast<LPWSTR>(ap.c_str()), SE_FILE_OBJECT, reqFlags, &owner, &group, &dacl, &sacl, &sd);

        if (res != ERROR_SUCCESS || sd == nullptr) {
            if (sd)
                LocalFree(sd);
            return false;
        }
    }

    // Convert to self-relative security descriptor bytes (if needed)
    DWORD needed = 0;
    if (!MakeSelfRelativeSD(sd, nullptr, &needed) && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        result.resize(needed);
        if (!MakeSelfRelativeSD(sd, reinterpret_cast<PSECURITY_DESCRIPTOR>(result.data()), &needed)) {
            LocalFree(sd);
            result.clear();
            return false;
        }
        result.resize(needed);
    } else {
        // sd may already be self-relative or MakeSelfRelativeSD behaved unexpectedly: copy raw
        DWORD len = GetSecurityDescriptorLength(sd);
        if (len == 0) {
            LocalFree(sd);
            return false;
        }
        result.resize(len);
        memcpy(result.data(), sd, len);
    }

    LocalFree(sd);
    return !result.empty();
    
}

// Never follows symlinks
bool set_acl(const STRING &path, const std::string &data) {
    if (data.empty()) {
        return true;
    }

    STRING ap = abs_path(path);
    if (ap.empty())
        ap = path;

    PSECURITY_DESCRIPTOR sd = reinterpret_cast<PSECURITY_DESCRIPTOR>(const_cast<char *>(data.data()));
    if (!sd)
        return false;

    PSID owner = nullptr, group = nullptr;
    PACL dacl = nullptr, sacl = nullptr;
    BOOL ownerDefaulted = FALSE, groupDefaulted = FALSE;
    BOOL daclPresent = FALSE, daclDefaulted = FALSE;
    BOOL saclPresent = FALSE, saclDefaulted = FALSE;

    SECURITY_DESCRIPTOR_CONTROL control;
    DWORD revision;

    GetSecurityDescriptorOwner(sd, &owner, &ownerDefaulted);
    GetSecurityDescriptorGroup(sd, &group, &groupDefaulted);
    GetSecurityDescriptorDacl(sd, &daclPresent, &dacl, &daclDefaulted);
    GetSecurityDescriptorSacl(sd, &saclPresent, &sacl, &saclDefaulted);
    GetSecurityDescriptorControl(sd, &control, &revision);

    bool needSacl = (sacl && saclPresent);

    DWORD flags = 0;

    if (owner) {
        flags |= OWNER_SECURITY_INFORMATION;
    }
    if (group) {
        flags |= GROUP_SECURITY_INFORMATION;
    }
    if (dacl && daclPresent) {
        flags |= DACL_SECURITY_INFORMATION;
    }
    if (needSacl) {
        flags |= SACL_SECURITY_INFORMATION;
    }


    if (control & SE_SACL_PROTECTED) {
        flags |= PROTECTED_SACL_SECURITY_INFORMATION;
    } else {
        flags |= UNPROTECTED_SACL_SECURITY_INFORMATION;
    }

    if (control & SE_DACL_PROTECTED) {
        flags |= PROTECTED_DACL_SECURITY_INFORMATION;
    } else {
        flags |= UNPROTECTED_DACL_SECURITY_INFORMATION;
    }

    if (flags == 0) {
        return true;
    }

    flags |= LABEL_SECURITY_INFORMATION | ATTRIBUTE_SECURITY_INFORMATION | SCOPE_SECURITY_INFORMATION | BACKUP_SECURITY_INFORMATION;

    if (true) {
        // Works for symlinks, dirs and files
        HANDLE hLink = CreateFileW(path.c_str(), READ_CONTROL | WRITE_DAC | WRITE_OWNER | ACCESS_SYSTEM_SECURITY,  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hLink == INVALID_HANDLE_VALUE) {
            return false;
        }

        DWORD err = SetSecurityInfo(hLink, SE_FILE_OBJECT, flags, owner, group, dacl, (flags & SACL_SECURITY_INFORMATION) ? sacl : nullptr);
        if (err != ERROR_SUCCESS) {
            CloseHandle(hLink);
            return false;
        }
        CloseHandle(hLink);
        return true;
    } 
    else {
        // Works for dirs and files, but not symlinks
        DWORD err = SetNamedSecurityInfoW(const_cast<LPWSTR>(ap.c_str()), SE_FILE_OBJECT, flags, owner, group, dacl, (flags & SACL_SECURITY_INFORMATION) ? sacl : nullptr);

        if (err != ERROR_SUCCESS) {
            return false;
        }

        return true;
    }
}

bool PrivilegeGuard::enable(const std::vector<LPCWSTR> &names) {
    if (token)
        return granted;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        token = nullptr;
        return false;
    }

    // build TOKEN_PRIVILEGES structure in a dynamic buffer
    DWORD n = static_cast<DWORD>(names.size());
    if (n == 0) {
        CloseHandle(token);
        token = nullptr;
        return false;
    }

    size_t tpSize = sizeof(TOKEN_PRIVILEGES) + (n - 1) * sizeof(LUID_AND_ATTRIBUTES);
    std::vector<BYTE> tpbuf(tpSize);
    PTOKEN_PRIVILEGES tp = reinterpret_cast<PTOKEN_PRIVILEGES>(tpbuf.data());
    tp->PrivilegeCount = n;

    for (DWORD i = 0; i < n; ++i) {
        if (!LookupPrivilegeValueW(nullptr, names[i], &tp->Privileges[i].Luid)) {
            // cleanup and fail
            CloseHandle(token);
            token = nullptr;
            return false;
        }
        tp->Privileges[i].Attributes = SE_PRIVILEGE_ENABLED;
    }

    // First call to get the size required to receive previous state
    DWORD needed = 0;
    AdjustTokenPrivileges(token, FALSE, tp, 0, nullptr, &needed);

    if (needed == 0) {
        // try enabling without retrieving previous state
        if (!AdjustTokenPrivileges(token, FALSE, tp, 0, nullptr, &needed)) {
            CloseHandle(token);
            token = nullptr;
            return false;
        }
        // previous not captured
        previous.clear();
        previousLen = 0;
    } else {
        previousLen = needed;
        previous.resize(previousLen);
        if (!AdjustTokenPrivileges(token, FALSE, tp, previousLen, reinterpret_cast<PTOKEN_PRIVILEGES>(previous.data()), &previousLen)) {
            previous.clear();
            previousLen = 0;
            CloseHandle(token);
            token = nullptr;
            return false;
        }
    }

    // Determine whether the privileges were actually granted
    if (GetLastError() == ERROR_SUCCESS) {
        granted = true;
    } else {
        granted = false;
    }

    return true;
}

PrivilegeGuard::~PrivilegeGuard() {
    if (token) {
        if (!previous.empty() && previousLen > 0) {
            // restore previous privileges (best-effort)
            AdjustTokenPrivileges(token, FALSE, reinterpret_cast<PTOKEN_PRIVILEGES>(previous.data()), previousLen, nullptr, nullptr);
        }
        CloseHandle(token);
        token = nullptr;
    }
}

#endif
