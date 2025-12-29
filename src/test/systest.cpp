#pragma execution_character_set("utf-8")

#include <chrono>
#include <thread>
#include <array>
#include <iostream>
#include <format>
#include <map>
#include <regex>
#include <vector>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#include <shlobj_core.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <wordexp.h>
#endif

#include <locale>
#include <codecvt>
#include <string>

#include "catch.hpp"
#include "../utilities.hpp"

using namespace std;

namespace {

string p(string path);
#ifdef _WIN32
string nul = "2>NUL";
bool win = true;
#define pclose _pclose
#define popen _popen
#else
bool win = false;
string nul = "2>/dev/null";
#endif

string root = filesystem::path(APP_PATH).parent_path().string();
string work = root + "/tmp";
string bin = APP_PATH;

// No need to edit
string tmp = work + "/tmp";
string in = tmp + "/in";
string out = tmp + "/out";
string full = tmp + "/full";
string diff = tmp + "/diff";
string testfiles = string(SRC_PATH) + "/../test/testfiles";
string diff_tool = win ? root + "/test/diffexe/diff.exe" : "diff";

#ifdef _WIN32

std::string w2utf8(const std::wstring &wstr) {
    if (wstr.empty())
        return {};
    int requiredSize = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (requiredSize <= 0) {
        throw std::runtime_error("Failed to calculate UTF-8 buffer size");
    }
    std::string utf8str(requiredSize, '\0');
    int result = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &utf8str[0], requiredSize, nullptr, nullptr);
    if (result != requiredSize) {
        throw std::runtime_error("UTF-8 conversion failed");
    }
    return utf8str;
}

std::wstring utf8w(const std::string& utf8str) {
    if (utf8str.empty())
        return {};
    int requiredSize = MultiByteToWideChar(CP_UTF8, 0, utf8str.data(), static_cast<int>(utf8str.size()), nullptr, 0);
    if (requiredSize <= 0) {
        throw std::runtime_error("Failed to convert UTF-8 to wide string (size calc)");
    }
    std::wstring wideStr(requiredSize, 0);
    int converted = MultiByteToWideChar(CP_UTF8, 0, utf8str.data(), static_cast<int>(utf8str.size()), &wideStr[0], requiredSize);
    if (converted != requiredSize) {
        throw std::runtime_error("Failed to convert UTF-8 to wide string (conversion)");
    }
    return wideStr;
};

#else
std::string w2utf8(const std::string &wstr) { return wstr; }
std::string utf8w(const std::string &utf8str) { return utf8str; }

#endif

template<typename... Args> std::string conc(const Args&... args) {
    std::ostringstream oss;
    ((oss << args << ' '), ...);
    string cmd2 = oss.str();
    cmd2.pop_back();
    return cmd2;
}




// 1. Definition af den faktiske implementering
template <typename... Args> std::string sys_impl(int *ret, const Args &...args) {
#ifdef _WIN32
    std::vector<wchar_t> buffer;
    buffer.resize(1000000);
    STRING result;
    string cmd2 = conc(args...);
    wstring cmd = utf8w(cmd2);
    FILE *pipe = _wpopen(wstring(cmd.begin(), cmd.end()).c_str(), L"r");
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgetws(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }
    int r = _pclose(pipe);
    if (ret) {
        *ret = r;
    }
    return w2utf8(result);
#else
    std::string result;
    char buffer[128];
    string cmd = conc(args...);
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw "Error executing command (at popen())";
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    if (pclose(pipe) == -1) {
        throw "Error executing command (at pclose())";
    }
    return result;
#endif
}

string p(string path) {
    if (win) {
        std::ranges::replace(path, '/', '\\');
    }
#ifndef _WIN32
    // resolve ~
    wordexp_t expResult;
    wordexp(path.c_str(), &expResult, 0);
    std::filesystem::path resolvedPath(expResult.we_wordv[0]);
    wordfree(&expResult);
    path = resolvedPath.string();
#endif
    return path;
}

// 2. Overload der tager imod ret-pointeren eksplicit
template <typename... Args> std::string sys(int *ret, const Args &...args) { return sys_impl(ret, args...); }

// 3. Overload der mangler ret-pointeren (kalder den ovenover med nullptr)
template <typename... Args> std::string sys(const Args &...args) {
    // Ved at bruge sys<Args...>(nullptr, args...) undgår vi tvivl,
    // men det sikreste er at have forskellige navne eller tydelige typer.
    return sys_impl(nullptr, args...);
}



void rm(string path) {
    path = p(path);
    REQUIRE((path.find("/tmp") != string::npos || path.find("\\tmp") != string::npos));
    if (win) {
        // fs::is_link() doesn't work for broken link to directory
        sys("rmdir /q/s", path, nul);
        sys("del", path, nul);
    }
    else {
        sys("rm -rf", path);
    }
}

void cp(string src, string dst) {
    sys(win ? "copy" : "cp", p(src), p(dst));
}

void rename(string dir, string old_name, string new_name) { 
    sys(win ? "rename" : "mv", p(dir + "/") + old_name, win ? new_name : (p(dir + "/") + new_name));
}

void md(string dir) { std::filesystem::create_directories(p(dir)); }

void clean_in_out() { 
    rm(in);
    rm(out);
    md(in);
    md(out);
}

void clean() {
    rm(tmp);
    sys("mkdir", p(work), nul);
    sys("mkdir", p(tmp));
    sys("mkdir", p(in));
    sys("mkdir", p(out));
}

bool can_create_links() {
#ifdef _WIN32
    bool b = IsUserAnAdmin();
    if(!b) {
        cerr << "*** Cannot create symlink. Please run Visual Studio or Command Prompt as Administrator ***\n";
        CHECK(false);
    }
    return b;
#else
    return true;
#endif
}

void lf(string from, string to, [[maybe_unused]] bool is_dir = false) {
    can_create_links();
    from = p(from);
    to = p(to);
    if(win) {
        sys("mklink", is_dir ? "/D" : "", from, to);        
    }
    else {
        sys("ln -s", to, from);        
    }   
}

void ld(string from, string to) {
    can_create_links();
    from = p(from);
    to = p(to);
    if(win) {
        sys("mklink /D", from, to);        
    }
    else {
        sys("ln -s", to, from);        
    }
}

void pick(string file, string dir = "") {
    file = testfiles + "/" + file;
    std::filesystem::create_directories(p(in + "/" + dir));
    cp(file, in + "/" + dir);
}

template<typename... Args> void ex(const Args&... args) {
    sys(p(bin), conc(args...));
}

bool cmp_diff(bool check = true) {
    auto ret = sys(diff_tool, "--no-dereference -r", in, out, "2>&1");
    if (check) {
        CHECK(ret.empty());
    }
    return ret.empty();
}

// On Windows: Tells <SYMLINK> vs <SYMLINKD> and timestamp, which "diff" does not look at
bool cmp_meta() {
    string ls_in = sys(win ? "dir /s" : "ls -l", p(in));
    string ls_out = sys(win ? "dir /s" : "ls -l", p(out));

    if(win) {
        for(auto s : vector<string*>{&ls_in, &ls_out}) {
            *s = std::regex_replace(*s, std::regex(R"(.* Directory of.*)"), "");
            *s = std::regex_replace(*s, std::regex(R"(.* bytes free.*)"), "");
            *s = std::regex_replace(*s, std::regex(R"(.* \..*)"), "");
        }
        
        if (ls_in != ls_out) {
            cerr << "\n[" << ls_in << "]\n";
            cerr << "\n[" << ls_out << "\n]";
        }

        return ls_in == ls_out;
    } else {
        string d = sys("rsync -aHcn --itemize-changes ", p(in) + "/", " ", p(out) + "/");
        if (d == ".d..t...... ./\n") {
            d = "";
        }

        if (d == ".d..t....... ./\n") {
            d = "";
        }

        if (!d.empty()) {
            cerr << "\n[" << d << "]\n";
        }
        return d.empty();
    }
}

bool cmp(bool check = true) {
    bool ret;
    if(win) {
        ret = cmp_diff() && cmp_meta();
    }
    else {
        ret = cmp_diff() && cmp_meta();
    }
    if (check) {
        REQUIRE(ret);
    }
    return ret;
}

size_t siz(string file) {   
    return filesystem::file_size(p(file));
}

void all_types() {
    md(in + "/d"); // dir
    pick("a"); // file
    lf(in + "/link_to_a", in + "/a"); // link to file
    ld(in + "/link_to_d", in + "/d"); // link to dir
    lf(in + "/link_to_missing_file", in + "/missing_file"); // broken link to file  
    ld(in + "/link_to_missing_dir", in + "/missing_dir"); // broken link to dir
}

void modify(string file) {
    file = p(file);
    sys("echo a>>", file);        
}
}

TEST_CASE("buildinfo1") { ex("-B"); }

TEST_CASE("compress from stdin and restore to stdout") {
    clean();
    pick("a");
    ex("-m1", "-stdin", "-stdout", "<", in + "/a", ">", full);
    ex("-R0", full, "-stdout", "<", full, ">", out + "/a");
    cmp_diff(); // timestamp cannot match
}

TEST_CASE("no ~ in paths") {
    // abs_path() and possibly other functions cannot handle the ~ character
    for (auto &p : vector<string>{root, work, bin, tmp, in, out, full, diff, testfiles}) {
        CHECK(p.find('~') == std::string::npos);
    }
}

TEST_CASE("no source files") {
    clean();
    ex("-m1", in, out + "/d");
    CHECK(!std::filesystem::exists(out + "d"));
    ex("-m1", in, "-stdout");
}

TEST_CASE("traverse") {
    // Traverse into d despite of -r flag because it was passed explicitly on the command line
    clean();
    md(in + "/d");
    cp(testfiles + "/a", in + "/d/a");
    ex("-m1r", in + "/d", full);
    ex("-R0f", full, out + "/d");
    cmp();

    // Wildcard must be seen as being passed explicitly (mostly for Windows that has no expansion)
    clean();
    md(in + "/d");
    cp(testfiles + "/a", in + "/d/a");
    ex("-m1r", in + "/*", full);
    ex("-R0f", full, out + "/d");
    cmp();
}

TEST_CASE("lua is_arg") {
    clean();
    md(in + "/d");
    cp(testfiles + "/a", in + "/d/a");
    ex("-m1r", "-u\"return(is_arg or (not is_dir))\"", in + "/d", full);
    ex("-R0f", full, out + "/d");
    cmp();

    clean();
    pick("a");
    pick("b");
    ex("-m1r", "-u\"return(is_arg)\"", in + "/a", full);
    ex("-R0f", full, out);
    rm(in + "/b");
    cmp();
}

TEST_CASE("overwrite during restore") {  
    // Overwrite
    clean();
    pick("a");
    cp(testfiles + "/b", out + "/a");
    ex("-m1", in, full);
    ex("-R0f", full, out);
    cmp();

    // Abort
    clean();
    pick("a");
    ex("-m1", in, full);
    rm(in + "/a");
    cp(testfiles + "/b", in + "/a");
    cp(testfiles + "/b", out + "/a");
    ex("-R0", full, out);
    cmp_diff();
}

TEST_CASE("overwrite during backup") {  
    // Overwrite
    clean();
    pick("a");
    ex("-m1", in, full);
    pick("b");
    ex("-m1f", in, full);
    ex("-R0", full, out);
    cmp_diff();

    // Abort
    clean();
    pick("a");
    ex("-m1", in, full);
    pick("b");
    ex("-m1", in, full);
    rm(in + "/b");
    ex("-R0", full, out);
    cmp_diff();
}

TEST_CASE("skip item prefixed with -- as relative path") {   
    auto skip = GENERATE("a", "d", "link_to_a", "link_to_d", "link_to_missing_file", "link_to_missing_dir");
    SECTION("") {
        clean();
        all_types();
        string path = p(in + "/" + skip);
        ex("-m1", "--" + path, in, full);
        ex("-R0", full, out);
        rm(path);
    }
    cmp();
}

TEST_CASE("skip item prefixed with -- as absolute path") {
    auto skip = GENERATE("a", "d", "link_to_a", "link_to_d", "link_to_missing_file", "link_to_missing_dir");
    SECTION("") {
        clean();
        all_types();
        string path = p(in + "/" + skip);
        path = std::filesystem::absolute(path).string();
        ex("-m1", "--" + path, in, full);
        ex("-R0", full, out);
        rm(path);
    }
    cmp();
}

#ifdef _WIN32
TEST_CASE("skip item prefixed with -- in different case") {   
    auto skip = GENERATE("A", "D", "Link_to_a", "Link_to_d", "Link_to_missing_file", "Link_to_missing_dir");
    SECTION("") {
        clean();
        all_types();
        string path = p(in + "/" + skip);
        path = std::filesystem::absolute(path).string();
        ex("-m1", "--" + path, in, full);
        ex("-R0", full, out);
        rm(path);
    }
    cmp();
}
#endif


TEST_CASE("case rename") {
    clean();
    pick("a");
    ex("-m1", in, full);    
    rename(in, "a","A");
    ex(in, full);
    rm(out);
    ex("-R1", full, out);
    cmp();
}

TEST_CASE("compression levels") {
    std::string x = GENERATE("-x0", "-x1", "-x2", "-x3", "-x4", "-x5");
    clean();
    md(in);
    std::ofstream(in + "/t") << "The quick brown fox jumps over the lazy dog";
    ex("-m1", x, in, full);
    ex("-R0", full, out);
    if (x != "-x5") {
        cmp();
    } else {
        INFO("Expected to fail for -x5");
        clean();
        md(in);
        cmp();
    }
}

TEST_CASE("simple backup, diff backup and restore") {
    clean();
    pick("a");
    ex("-m1", in, full);
    ex("-R0", full, out);
    cmp();

    ex(in, full);
    rm(out);
    ex("-R1", full, out);
    cmp();
}

TEST_CASE("no -g or -m flag during incremental backup") {
    clean();
    pick("a");
    ex("-m1", in, full);
    ex(in, full);
    rm(out);
    ex("-R1", full, out);
    cmp();
}

TEST_CASE("diff size") {
    // Diff must be tiny without -w and every unique filename should be stored just once
    clean();
    pick("high_entropy_a");
    for(int i = 0; i < 30; i++) {
        cp(in + "/high_entropy_a", in + "/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" + std::to_string(i));
    }
    ex("-m1", in, full);
    size_t f = siz(full);
    ex(in, full);
    CHECK(siz(full) - f < 500);
    ex("-R0", full, out);
    cmp();
}

TEST_CASE("w flag") {
    {
        // Diff must be smaller without -w than with. Use -t1 for determinism
        clean();
        pick("high_entropy_a");
        ex("-m1t1", in, full);
        auto s1 = siz(full);
        ex(in, full);
        auto s2 = siz(full);
        ex("-wt1", in, full);
        auto s3 = siz(full);
        CHECK(s3 - s2 > (s2 - s1) + 50);
        ex("-R1", full, out);
        cmp();
        rm(out);
        ex("-R2", full, out);
        cmp();
    }
#ifdef _WIN32
    {
        // Recognize file is unchanged despite being passed in different case
        clean();
        pick("high_entropy_a");
        string a = filesystem::absolute(in).string();
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::toupper(c); });
        ex("-m1", a, full);
        auto s1 = siz(full);
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::tolower(c); });
        ex(in, full);
        auto s2 = siz(full);
        ex("-w", a, full);
        auto s3 = siz(full);
        CHECK((s3 > s2 / 2));
    }
#endif

    // Recognize that file has changed
    modify(in + "/high_entropy_a");
    rm(out);
    ex("-w", in, full);
    ex("-R3", full, out);
    cmp();
}

TEST_CASE("destination directory doesn't exist") {
    clean();
    pick("a");
    ex("-m1",in, full);
    rm(out);
    ex("-R0", full, out);
    cmp();
    ex(in, full);
    rm(out);
    ex("-R1", full, out);
    cmp();
}

TEST_CASE("unicode") {
    clean();
    
    // todo, test many more unicode sections
    SECTION("latin") {
        pick("æøåäöüßéèáéíóúüñ");
    }

    SECTION("hanja") {
        pick("운일암반계곡");
    }

    ex("-m1",in, full);
    ex("-R0", full, out);
    cmp();
}

TEST_CASE("lua all or none") {
    clean();
    all_types();
    pick("æøåäöüßéèáéíóúüñ");

    SECTION("all") {
        ex("-m1 -u\"return true\"", in, full);
    }
    SECTION("none") {
        // no full file created, so we expect restore to fail
        md(out); // create dir manually because restore will fail
        ex("-m1 -u\"return false\"", in, full);
        rm(in);
        md(in); // empty in dir
    }

    ex("-R0", full, out); 
    cmp_diff();
}

TEST_CASE("lua types") {
    clean();
    all_types();
    
    SECTION("add only dir") {
        rm(in);
        md(in);
        md(in + "/d");
        ex("-m1 -u\"return is_dir\"", in, full);
    }
    SECTION("add only file") {
        rm(in);
        md(in);
        pick("a");
        ex("-m1 -u\"return is_file or is_arg\"", in, full);
    }

    // todo, links

    ex("-R0", full, out);
    cmp_diff();
}

TEST_CASE("lua contains") {
    clean();
    pick("a");
    pick("b");
    ex("-m1", "-u\"return(is_dir or contains({'a'}, name))\"", in, full);
    rm(in + "/b");
    ex("-R0", full, out); 
    cmp();
}

TEST_CASE("lua utf8") {
    clean();
    pick("æøåäöüßéèáéíóúüñ");
    pick("운일암반계곡");
    ex("-m1", "-u\"return(name ~= 'a')\"", in, full);
    ex("-R0", full, out); 
    cmp();
}

TEST_CASE("lua no case conversion") {
    // No longer pass lower case to Lua
    clean();
    pick("a");
    cp(in + "/a", in + "/AAA");
    ex("-m1v3", "-u\"return(name ~= 'aaa')\"", in, full);
    ex("-R0", full, out); 
    cmp();
}

TEST_CASE("deduplication") {
    clean();
    auto i = GENERATE("", "-h");

    SECTION("duplicated data detcted within full backup") {
        pick("high_entropy_a", "dir1"); // 65811 bytes
        pick("high_entropy_a", "dir2");
        ex("-m1x0", string(i), in, full);
        CHECK(((siz(full) > 65811) && siz(full) < 76000));
    }

    SECTION("duplicated data detected between full backup and diff backup") {
        pick("high_entropy_a", "dir1"); // 65811 bytes
        ex("-m1x0",in, full);
        size_t f = siz(full);
        pick("high_entropy_a", "dir2"); // 65811 bytes
        ex(in, full);
        size_t d = siz(full) - f;
        CHECK(((d > 100) && d < 8000));
    }

    SECTION("duplicated data detected within diff backup") {
        pick("a");
        ex("-m1x0",in, full);
        size_t f = siz(full);
        pick("high_entropy_a", "dir1"); // 65811 bytes
        pick("high_entropy_a", "dir2");
        ex(in, full);
        CHECK(((siz(full) - f > 65811) && siz(full) - f < 76000));
    } 
}

TEST_CASE("symlink to dir") {
    clean();
    md(in + "/d");
    ld(in + "/link_to_d", in + "/d");
    ex("-m1",in, full);
    ex("-R0", full, out);
    cmp();
}

TEST_CASE("broken symlink to dir") {
    clean();
    ld(in + "/link_to_missing_dir", in + "/missing_dir");
    ex("-m1",in, full);
    ex("-R0", full, out);
    cmp();
}

TEST_CASE("symlink to file") {
    clean();
    pick("a");
    lf(in + "/link_to_a", in + "/a");
    ex("-m1",in, full);
    ex("-R0", full, out);
    cmp();
}

TEST_CASE("broken symlink to file") {
    clean();
    lf(in + "/link_to_missing_file", in + "/missing_file");
    ex("-m1",in, full);
    ex("-R0", full, out);
    cmp();
}

TEST_CASE("follow symlinks") {
    clean();
    pick("a");
    md(in + "/d");
    lf(in + "/link_to_a", in + "/a");
    lf(in + "/link_to_d", in + "/d", true);
    ex("-m1h", in, full);
    ex("-R0", full, out);
    rm(in + "/link_to_a");
    rm(in + "/link_to_d");
    cp(in + "/a", in + "/link_to_a");
    md(in + "/link_to_d");
    cmp_diff();
}

#ifdef _WIN32
TEST_CASE("follow mismatching symlink to directory") {
    // symlink to file points at directory, so skip due to -c flag
    clean();
    pick("a");
    md(in + "/d");
    lf(in + "/link_to_d", in + "/d");
    ex("-m1hc", in, full);
    ex("-R0", full, out);
    rm(in + "/link_to_d");
    cmp_diff();
}

TEST_CASE("follow mismatching symlink to file") {
    // symlink to directory points at file, so skip due to -c flag
    clean();
    pick("a");
    md(in + "/d");
    lf(in + "/link_to_a", in + "/a", true);
    ex("-m1hc", in, full);
    ex("-R0", full, out);
    rm(in + "/link_to_a");
    cmp_diff();
}
#endif

TEST_CASE("timestamps") {
    clean();
    all_types();
    ex("-m1", in, full);
    ex("-R0", full, out);
    cmp();
}

TEST_CASE("restore from stdin by redirection") {
    clean();
    pick("a");
    ex("-m1", in, full);
    ex("-R0", "-stdin", out, "<", full);
    cmp();
}

TEST_CASE("restore from stdin by pipe") {
    clean();
    pick("a");
    ex("-m1", in, "-stdout", "|", p(bin), "-R0 -stdin", out);
    cmp();
}

TEST_CASE("compress from stdin by redirection") {
    clean();
    cp(testfiles + "/a", in + "/stdin");   
    ex("-m1", "-stdin", full, "<", p(in + "/stdin"));
    ex("-R0", full, out);
    cmp_diff();
}

TEST_CASE("compress from stdin by pipe") {
    clean();
    cp(testfiles + "/a", in + "/stdin");
    sys(win ? "type" : "cat", p(in + "/stdin"), "|", p(bin), "-m1", "-stdin", full);
    ex("-R0", full, out);
    cmp_diff();
}



TEST_CASE("test of tests") {
    clean();
    pick("a");
    ex("-m1", in, full);
    ex("-R0", full, out);
    CHECK(set_date(s2w(in + "/a"), 946684800'000));
    time_ms_t modified = get_date(s2w(in + "/a")).written;
    CHECK(modified == 946684800'000);
    CHECK(!cmp(false));
}


TEST_CASE("preserve dates") { 
    clean();
    pick("a");
    md(in + "/d");
    CHECK(set_date(s2w(in + "/a"), 946684800'000));
    CHECK(set_date(s2w(in + "/d"), 946684800'000));
    ex("-m1", in, full);
    ex("-R0", full, out);
    cmp();
}

TEST_CASE("absolute paths") {
    clean();
    cp(testfiles + "/a", in + "/a");
    md(in + "/d");
    cp(testfiles + "/b", in + "/d/b");
    lf(in + "/link_to_a", in + "/a");
    lf(in + "/link_to_b", in + "/d/b");
    ex("-m1a", in, full);
    auto f = w2utf8(abs_path(s2w(in)));

    SECTION("full path") {
        ex("-R0", full, out, f);
        cmp();
    }
    SECTION("sub directory") {
        clean_in_out();
        pick("b");
        ex("-R0", full, out, f + p("/d"));
        cmp_diff();
    }
}

TEST_CASE("absolute paths with -h flag") {
    clean();
    cp(testfiles + "/a", in + "/a");
    md(in + "/d");
    cp(testfiles + "/b", in + "/d/b");
    lf(in + "/link_to_a", in + "/a");
    lf(in + "/link_to_b", in + "/d/b");
    ex("-m1ah", in, full);
    auto f = w2utf8(abs_path(s2w(in)));

    SECTION("full path") {
        clean_in_out();
        cp(testfiles + "/a", in + "/a");
        md(in + "/d");
        cp(testfiles + "/b", in + "/d/b");
        cp(testfiles + "/a", in + "/link_to_a");
        cp(testfiles + "/b", in + "/link_to_b");
        ex("-R0", full, out, f);
        cmp_diff();
    }

    SECTION("sub directory") {
        clean_in_out();
        pick("b");
        ex("-R0", full, out, f + p("/d"));
        cmp_diff();
    }
}

TEST_CASE("add to aborted") {
    clean();
    pick("a");
    ex("-m1", in, full);
    ex("-m1", in, in + "/non-existant-to-trigger-abort", full);
    ex("-m1", in, full);
    ex("-R1", full, out);
    cmp_diff();
}

#ifndef _WIN32
TEST_CASE("lua unix filenames") {
    // Create file with following name, including the double quotes: ";f();x="
    // If not escaped correctly, it will create the Lua line: name = "";f();x=""
    // lua_load() won't catch it because it's valid syntax, so it will instead panic
    // at Lua-runtime because f() is not a defined function
    clean();
    cp(testfiles + "/a", in + R"===(/'\"\;f\(\)\;x=\"')===");
    // cerr << sys("ls -R0", in);
    ex("-m1", "-u\"return(is_file or is_dir)\"", in, full);
    ex("-R0", full, out); 
    cmp();

    // Try all bytes that are valid in a UNIX filename
    clean();
    for(int c = 0; c < 256; c++) {
        if(c == '/' || c == '\0') {
            continue;
        }

        std::string fileName = p(in) + "/" + char(c);
        std::ofstream outputFile(fileName);
        outputFile.close();

    }
  //  cerr << sys("ls -R0", in);
    ex("-m1", "-u\"return(name == 'tmp' or name == 'full' or name == 'out' or name == 'in' or len(name) == 1)\"", in, full);
    ex("-R0", full, out); 
    cmp();
}

TEST_CASE("skip domain socket") {
    clean();
    pick("a");
    sys("timeout 0.1 nc -lU " + in + "/domain_socket");
    ex("-m1",in, full);
    ex("-R0", full, out);
    rm(in + "/domain_socket");
    cmp();
}


TEST_CASE("include link to domain socket") {
    clean();
    pick("a");
    sys("timeout 0.1 nc -lU " + in + "/domain_socket");
    lf(in + "/link_to_domain_socket", in + "/domain_socket");
    ex("-m1",in, full);
    ex("-R0", full, out);
    rm(in + "/domain_socket");
    cmp();
}

TEST_CASE("skip link to domain socket") {
    clean();
    pick("a");
    sys("timeout 0.1 nc -lU " + in + "/domain_socket");
    lf(in + "/link_to_domain_socket", in + "/domain_socket");
    ex("-m1 -h",in, full);
    ex("-R0", full, out);
    rm(in + "/domain_socket");
    rm(in + "/link_to_domain_socket");
    cmp();
}


#endif

TEST_CASE("buildinfo2") {
    ex("-B");
}

TEST_CASE("acl_roundtrip_verify") {
    bool pipe = GENERATE(true, false);

    clean();
    md(in);

    std::filesystem::create_directories(p(in));
    {
        std::ofstream f(p(in + "/f1.txt"));
        f << "hello acl test\n";
    }
    md(in + "/d1");
    {
        std::ofstream f(p(in + "/d1/f2.txt"));
        f << "inner file\n";
    }

    lf(in + "/link_to_f1", in + "/f1.txt");
    ld(in + "/link_to_d1", in + "/d1");

#ifdef _WIN32
    auto set_acl = [](const std::string &path, const std::string &aceString) {
        size_t colon = aceString.find(':');
        if (colon == std::string::npos) {
            return 1;
        }
        std::string principal = aceString.substr(0, colon);
        std::string rights = aceString.substr(colon + 1);
        std::string cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"";
        cmd += "$acl = Get-Acl -LiteralPath '" + path + "'; ";
        cmd += "$rule = New-Object System.Security.AccessControl.FileSystemAccessRule('" + principal + "', '" + rights + "', 'Allow'); ";
        cmd += "$acl.AddAccessRule($rule); ";
        cmd += "Set-Acl -LiteralPath '" + path + "' $acl";
        cmd += "\"";
        cmd += " 2>&1";
        int ret = 1;
        auto s = sys(&ret, cmd);
        return ret;
    };

    CHECK(set_acl(in + "/non-existant", "Everyone:ReadAndExecute")); // test the system test
    CHECK(!set_acl(in + "/f1.txt", "Everyone:ReadAndExecute"));
    CHECK(!set_acl(in + "/d1", "Everyone:FullControl"));
    CHECK(!set_acl(in + "/d1/f2.txt", "Everyone:Modify"));
    CHECK(!set_acl(in + "/link_to_f1", "Everyone:ChangePermissions"));
    CHECK(!set_acl(in + "/link_to_d1", "Everyone:Traverse"));

#else
    chmod(p(in + "/d1").c_str(), S_IRWXU);
    chmod(p(in + "/d1/f2.txt").c_str(), S_IRUSR | S_IWUSR);
    chmod(p(in + "/f1.txt").c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    chmod(p(in + "/link_to_d1").c_str(), S_IRWXU); // no effect on Linux
    chmod(p(in + "/link_to_f1").c_str(), S_IRUSR | S_IWUSR); // no effect on Linux
#endif

    if (pipe) {
        ex("-m1C", in, "-stdout", ">", full);
        ex("-R0C", "-stdin", out, "<", full);
    } else {
        ex("-m1C", in, full);
        ex("-R0C", full, out);
    }

    // Wait for FS operations to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto read_file = [](const string &path) -> string {
        std::ifstream in(p(path), std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };

#ifdef _WIN32
    // Read icacls for a path and normalize it for comparison, don't follow links
    auto read_sddl = [&](const string &path) -> string {
        string cmd = "powershell.exe -NoProfile -Command \"(Get-Acl -LiteralPath '" + path + "').Sddl\"";
        string sddl = sys(cmd);
        sddl.erase(sddl.find_last_not_of("\r\n") + 1);
        return sddl;
    };
#endif

    auto is_symlink_local = [](const string &path) -> bool {
#ifdef _WIN32
        return is_symlink(s2w(path));
#else
        return std::filesystem::is_symlink(std::filesystem::path(p(path)));
#endif
    };

    vector<string> items = {"/f1.txt", "/d1", "/d1/f2.txt", "/link_to_f1", "/link_to_d1"};

    for (auto &rel : items) {
        string sitem = in + rel;
        string ditem = out + rel;

        REQUIRE(std::filesystem::exists(p(ditem)));

        bool s_is_symlink = is_symlink_local(sitem);
        bool d_is_symlink = is_symlink_local(ditem);
        CHECK(s_is_symlink == d_is_symlink);

        bool s_is_dir = std::filesystem::is_directory(std::filesystem::path(p(sitem)));
        bool d_is_dir = std::filesystem::is_directory(std::filesystem::path(p(sitem)));
        CHECK(s_is_dir == d_is_dir);

        if (!s_is_dir && !s_is_symlink) {
            if (std::filesystem::is_regular_file(std::filesystem::path(p(ditem)))) {
                string scont = read_file(sitem);
                string dcont = read_file(ditem);
                CHECK(scont == dcont);
            }
        }

#ifdef _WIN32
        string s_acl = read_sddl(sitem);
        string d_acl = read_sddl(ditem);
        CHECK((!s_acl.empty()) == (!d_acl.empty()));
        if (!s_acl.empty() && !d_acl.empty()) {
            CHECK(s_acl == d_acl);
        }
#else
        struct stat st_s, st_d;
        int rc_s = lstat(p(sitem).c_str(), &st_s);
        int rc_d = lstat(p(ditem).c_str(), &st_d);
        REQUIRE(rc_s == 0);
        REQUIRE(rc_d == 0);
        int perms_s = st_s.st_mode & 07777;
        int perms_d = st_d.st_mode & 07777;
        CHECK(perms_s == perms_d);
#endif
    }
}


#ifndef _WIN32
TEST_CASE("xattr_roundtrip") {
    clean();
    md(in);
    {
        std::ofstream f(p(in + "/f1.txt"));
        f << "hello xattr\n";
    }
    md(in + "/d1");
    {
        std::ofstream f(p(in + "/d1/f2.txt"));
        f << "inner\n";
    }

    lf(in + "/link_to_f1", in + "/f1.txt");
    ld(in + "/link_to_d1", in + "/d1");

    auto set_xattr_cmd = [&](const string &path, const string &name, const string &value, bool follow) {
        string q = string("\"") + p(path) + "\"";
        string hflag = follow ? "" : "-h";

        if (hflag.empty())
            sys("setfattr", "-n", name, "-v", value, q);
        else
            sys("setfattr", hflag, "-n", name, "-v", value, q);
    };

    auto get_xattr_hex = [&](const string &path, const string &name, bool follow) -> string {
        string q = string("\"") + p(path) + "\"";
        string hflag = follow ? "" : "-h";
        string out;
        if (hflag.empty())
            out = sys("getfattr", "-n", name, "-e", "hex", "-d", q, "2>/dev/null");
        else
            out = sys("getfattr", hflag, "-n", name, "-e", "hex", "-d", q, "2>/dev/null");

        std::istringstream iss(out);
        string line;
        while (std::getline(iss, line)) {
            auto eq = line.find('=');
            if (eq != string::npos && line.rfind("#", 0) != 0) {
                string val = line.substr(eq + 1);
                auto first = val.find_first_not_of(" \r\n\t");
                if (first == string::npos)
                    return string();
                auto last = val.find_last_not_of(" \r\n\t");
                return val.substr(first, last - first + 1);
            }
        }
        return string();
    };

    // two attributes
    set_xattr_cmd(in + "/f1.txt", "user.f1", "hello", true);
    set_xattr_cmd(in + "/f1.txt", "user.f2", "goodbye", true);

    // single attribute
    set_xattr_cmd(in + "/d1", "user.dir", "dirval", true);

    // no attributes
    ex("-m1X", in, full);
    ex("-R0Xc", full, out); // -c flag because xattrs on symlinks are platform dependent

    string s_f1_a = get_xattr_hex(in + "/f1.txt", "user.f1", true);
    string d_f1_a = get_xattr_hex(out + "/f1.txt", "user.f1", true);
    REQUIRE(!s_f1_a.empty());
    REQUIRE(!d_f1_a.empty());
    CHECK(s_f1_a == d_f1_a);

    string s_f1_b = get_xattr_hex(in + "/f1.txt", "user.f2", true);
    string d_f1_b = get_xattr_hex(out + "/f1.txt", "user.f2", true);
    REQUIRE(!s_f1_b.empty());
    REQUIRE(!d_f1_b.empty());
    CHECK(s_f1_b == d_f1_b);

    string s_d1_a = get_xattr_hex(in + "/d1", "user.dir", true);
    string d_d1_a = get_xattr_hex(out + "/d1", "user.dir", true);
    REQUIRE(!s_d1_a.empty());
    REQUIRE(!d_d1_a.empty());
    CHECK(s_d1_a == d_d1_a);

    string s_inner_none = get_xattr_hex(in + "/d1/f2.txt", "user.f2", true);
    string d_inner_none = get_xattr_hex(out + "/d1/f2.txt", "user.f2", true);
    CHECK(s_inner_none.empty());
    CHECK(d_inner_none.empty());
}

#endif

#ifdef _WIN32
TEST_CASE("windows_file_attributes") {
    clean();
    md(in);

    string file = in + "/file.txt";
    {
        std::ofstream f(p(file), std::ios::binary);
        f << "attribute test\n";
    }

    string qfile = string("\"") + p(file) + "\"";
    sys("attrib", "+R", "+H", "+S", "+A", qfile);
    sys("compact", "/c", qfile);
    ex("-m1C", in, full);
    ex("-R0C", full, out);

    // allow FS operations to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    string dfile = out + "/file.txt";
    string dqfile = string("\"") + p(dfile) + "\"";

    REQUIRE(std::filesystem::exists(p(dfile)));

    string attrib_out = sys("attrib", dqfile);
    CHECK(attrib_out.find('R') != string::npos);
    CHECK(attrib_out.find('H') != string::npos);
    CHECK(attrib_out.find('S') != string::npos);
    CHECK(attrib_out.find('A') != string::npos);

    string compact_out = sys("compact", dqfile);
    bool compressed = (compact_out.find("Compressed") != string::npos) || (compact_out.find("is compressed") != string::npos) || (compact_out.find("compressed") != string::npos);
    CHECK(compressed);
}
#endif

