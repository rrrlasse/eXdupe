
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

#ifdef _WIN32
#include <iostream>
#include <Windows.h>
#include <shellapi.h>
#include <shlobj_core.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <wordexp.h>
#endif

#include "catch.hpp"

#include <iostream>
#include <locale>
#include <codecvt>
#include <string>

using namespace std;

namespace {

string p(string path);

#include <iostream>
#include <filesystem>

#ifdef _WIN32
string nul = "2>NUL";
bool win = true;
#define pclose _pclose
#define popen _popen
#else
bool win = false;
string nul = "2>/dev/null";
#endif

// Please customize, use "/" for path delimitors on Windows
string root = win ? "e:/exdupe" : "/mnt/hgfs/E/eXdupe"; // the dir that contains README.md
string work = win ? "e:/exdupe/tmp" : "~/out/tmp"; // tests will read and write here, it must support symlinks
string bin = win ? "e:/exdupe/exdupe.exe" : "~/out/exdupe";

// No need to edit
string tmp = work + "/tmp";
string in = tmp + "/in";
string out = tmp + "/out";
string full = tmp + "/full";
string diff = tmp + "/diff";
string testfiles = root + "/test/testfiles";
string diff_tool = win ? root + "/test/diffexe/diff.exe" : "diff";

template<typename... Args> std::string conc(const Args&... args) {
    std::ostringstream oss;
    ((oss << args << ' '), ...);
    string cmd2 = oss.str();
    cmd2.pop_back();
    return cmd2;
}

template<typename... Args> std::string sys(const Args&... args) {
#ifdef _WIN32
    auto utf8w = [](std::string utf8str) {
        std::locale utf8_locale(std::locale(), new std::codecvt_utf8<wchar_t>);
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.from_bytes(utf8str);
    };
    std::array<char, 128> buffer;
    std::string result;
    string cmd2 = conc(args...);
    wstring cmd = utf8w(cmd2);
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_wpopen(wstring(cmd.begin(), cmd.end()).c_str(), L"r"), _pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
#else
    std::string result;
    char buffer[128];
    string cmd = conc(args...);
    FILE* pipe = popen(cmd.c_str(), "r");
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
    if(win) {
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

void rm(string path) {
    path = p(path);
    REQUIRE((path.find("/tmp/") != string::npos || path.find("\\tmp\\") != string::npos));
    if(win) {
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

void clean() {
    rm(tmp);
    sys("mkdir", p(work), nul);
    sys("mkdir", p(tmp));
    sys("mkdir", p(in));
    sys("mkdir", p(out));
}

void md(string dir) {
    std::filesystem::create_directories(p(dir));
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

void lf(string from, string to) {
    can_create_links();
    from = p(from);
    to = p(to);
    if(win) {
        sys("mklink", from, to);        
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

bool cmp_diff() {
    auto ret = sys(diff_tool, "--no-dereference -r", in, out, nul);
    return ret.empty();
}

// On Windows: Tells <SYMLINK> vs <SYMLINKD> and timestamp, which "diff" does not look at
bool cmp_ls() {
    string ls_in = sys(win ? "dir /s" : "ls -l", p(in));
    string ls_out = sys(win ? "dir /s" : "ls -l", p(out));

    if(win) {
        std::regex freeRegex(R"(.* Directory of.*)");
        std::regex filesRegex(R"(.* bytes free.*)");

        for(auto s : vector<string*>{&ls_in, &ls_out}) {
            *s = std::regex_replace(ls_in, freeRegex, "");
            *s = std::regex_replace(ls_in, filesRegex, "");
        }
    }

    CHECK(ls_in == ls_out);
    return ls_in == ls_out;
}

bool cmp() {
    bool ret;
    if(win) {
        ret = cmp_diff() && cmp_ls();
    }
    else {
        ret = cmp_diff();
    }
    CHECK(ret);
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
}

TEST_CASE("traverse") {
    // Traverse into d despite of -r flag because it was passed explicitly on the command line
    clean();
    md(in + "/d");
    cp(testfiles + "/a", in + "/d/a");
    ex("-m1r", in + "/d", full);
    ex("-Rf", full, out + "/d");
    cmp();

    // Wildcard must be seen as being passed explicitly (mostly for Windows that has no expansion)
    clean();
    md(in + "/d");
    cp(testfiles + "/a", in + "/d/a");
    ex("-m1r", in + "/*", full);
    ex("-Rf", full, out + "/d");
    cmp();
}

TEST_CASE("lua is_arg") {
    clean();
    md(in + "/d");
    cp(testfiles + "/a", in + "/d/a");
    ex("-m1r", "-u\"return(is_arg or (not is_dir))\"", in + "/d", full);
    ex("-Rf", full, out + "/d");
    cmp();

    clean();
    pick("a");
    pick("b");
    ex("-m1r", "-u\"return(is_arg)\"", in + "/a", full);
    ex("-Rf", full, out);
    rm(in + "/b");
    cmp();
}

TEST_CASE("overwrite during restore") {  
    // Overwrite
    clean();
    pick("a");
    cp(testfiles + "/b", out + "/a");
    ex("-m1", in, full);
    ex("-Rf", full, out);
    cmp();

    // Abort
    clean();
    pick("a");
    ex("-m1", in, full);
    cp(in + "/b", out + "/a");
    cp(out + "/b", out + "/a");
    ex("-R", full, out);
    cmp();
}

TEST_CASE("overwrite during backup") {  
    // Overwrite
    clean();
    pick("a");
    ex("-m1", in, full);
    pick("b");
    ex("-m1f", in, full);
    ex("-R", full, out);
    cmp();

    // Abort
    clean();
    pick("a");
    ex("-m1", in, full);
    pick("b");
    ex("-m1", in, full);
    rm(in + "/b");
    ex("-R", full, out);
    cmp();
}

TEST_CASE("skip item prefixed with -- as relative path") {   
    auto skip = GENERATE("a", "d", "link_to_a", "link_to_d", "link_to_missing_file", "link_to_missing_dir");
    SECTION("") {
        clean();
        all_types();
        string path = p(in + "/" + skip);
        ex("-m1", "--" + path, in, full);
        ex("-R", full, out);
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
        ex("-R", full, out);
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
        ex("-R", full, out);
        rm(path);
    }
    cmp();
}
#endif

TEST_CASE("simple backup, diff backup and restore") {
    clean();
    pick("a");
    ex("-m1", in, full);
    ex("-R", full, out);
    cmp();

    ex("-D", in, full, diff);
    rm(out);
    ex("-RD", full, diff, out);
    cmp();
}

TEST_CASE("destination dirctory doesn't exist") {
    clean();
    pick("a");
    ex("-m1",in, full);
    rm(out);
    ex("-R", full, out);
    cmp();
    ex("-D", in, full, diff);
    rm(out);
    ex("-RD", full, diff, out);
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
    ex("-R", full, out);
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
        ex("-m1 -u\"return false\"", in, full);
        rm(in);
    }

    ex("-R", full, out); 
    cmp();
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

    ex("-R", full, out);
    cmp();
}

TEST_CASE("lua contains") {
    clean();
    pick("a");
    pick("b");
    ex("-m1", "-u\"return(is_dir or contains({'a'}, name))\"", in, full);
    rm(in + "/b");
    ex("-R", full, out); 
    cmp();
}

TEST_CASE("lua utf8") {
    clean();
    pick("æøåäöüßéèáéíóúüñ");
    pick("운일암반계곡");
    ex("-m1", "-u\"return(name ~= 'a')\"", in, full);
    ex("-R", full, out); 
    cmp();
}

TEST_CASE("lua no case conversion") {
    // No longer pass lower case to Lua
    clean();
    pick("a");
    cp(in + "/a", in + "/AAA");
    ex("-m1v3", "-u\"return(name ~= 'aaa')\"", in, full);
    ex("-R", full, out); 
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
        pick("high_entropy_a", "dir2"); // 65811 bytes
        ex("-D", in, full, diff);
        CHECK(((siz(diff) > 100) && siz(diff) < 8000));
    }

    SECTION("duplicated data detected within diff backup") {
        pick("a");
        ex("-m1x0",in, full);
        pick("high_entropy_a", "dir1"); // 65811 bytes
        pick("high_entropy_a", "dir2");
        ex("-D", in, full, diff);
        CHECK(((siz(diff) > 65811) && siz(diff) < 76000));
    }
}

TEST_CASE("symlink to dir") {
    clean();
    md(in + "/d");
    ld(in + "/link_to_d", in + "/d");
    ex("-m1",in, full);
    ex("-R", full, out);
    cmp();
}

TEST_CASE("broken symlink to dir") {
    clean();
    ld(in + "/link_to_missing_dir", in + "/missing_dir");
    ex("-m1",in, full);
    ex("-R", full, out);
    cmp();
}

TEST_CASE("symlink to file") {
    clean();
    pick("a");
    lf(in + "/link_to_a", in + "/a");
    ex("-m1",in, full);
    ex("-R", full, out);
    cmp();
}

TEST_CASE("broken symlink to file") {
    clean();
    lf(in + "/link_to_missing_file", in + "/missing_file");
    ex("-m1",in, full);
    ex("-R", full, out);
    cmp();
}

TEST_CASE("timestamps") {
    clean();
    all_types();
    ex("-m1", in, full);
    ex("-R", full, out);
    cmp();
}

TEST_CASE("restore from stdin by redirection") {
    clean();
    pick("a");
    ex("-m1", in, full);
    ex("-R", "-stdin", out, "<", full);
    cmp();
}

TEST_CASE("restore from stdin by pipe") {
    clean();
    pick("a");
    ex("-m1", in, "-stdout", "|", p(bin), "-R -stdin", out);
    cmp();
}

TEST_CASE("compress from stdin by redirection") {
    clean();
    pick("a");
    ex("-m1", "-stdin", "a", full, "<", p(in + "/a"));
    ex("-R", full, out);
    cmp();
}

TEST_CASE("compress from stdin by pipe") {
    clean();
    pick("a");   
    sys(win ? "type" : "cat", p(in + "/a"), "|", p(bin), "-m1", "-stdin", "a", full);
    ex("-R", full, out);
    cmp();
}

TEST_CASE("compress from stdin and restorre to stdout") {
    clean();
    pick("a");
    ex("-m1", "-stdin", "a", "-stdout", "<", p(in + "/a"), ">", p(out + "/a"));
    cmp_diff(); // timestamp cannot match
}


