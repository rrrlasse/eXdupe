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
#endif

#include "catch.hpp"

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

// Please customize
string root = win ? "e:\\exdupe" : "/mnt/hgfs/E/eXdupe"; // the dir that contains README.md
string work = win ? "e:\\exdupe\\tmp" : "~/out/tmp"; // tests will read and write here, it must support symlinks
string diff_tool = win ? root + "test\\diffexe\\diff.exe" : "diff";
string bin = win ? "e:\\exdupe\\exdupe.exe" : "~/out/exdupe";

// No need to edit
string tmp = p(work + "/tmp");
string in = p(tmp + "/in");
string out = p(tmp + "/out");
string full = p(tmp + "/full");
string diff = p(tmp + "/diff");
string testfiles = p(root + "/test/testfiles");

template<typename... Args> std::string conc(const Args&... args) {
    std::ostringstream oss;
    ((oss << args << ' '), ...);
    string cmd2 = oss.str();
    cmd2.pop_back();
    return cmd2;
}

template<typename... Args> std::string sys(const Args&... args) {
    std::string result;
    char buffer[128];
    string cmd = conc(args...);
     //   cerr << "[" << cmd << "]\n";
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
}

string p(string path) {
    if(win) {
        std::ranges::replace(path, '/', '\\');
    }
    else {
        std::ranges::replace(path, '\\', '/');    
    }
    return path;
}

void rm(string path) {
    path = p(path);
    if(win) {
        // filesystem::is_link() does not work for broken link to directory, so
        // we cannot check what to delete and what command to use
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
    sys("mkdir", p(dir));    
}

bool can_create_links() {
#ifdef _WIN32
    bool b = IsUserAnAdmin();
    if(!b) {
        cerr << "Cannot create symlink. Please run Visual Studio or Command Prompt as Administrator\n";
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

void pick(string file) {
    file = testfiles + "/" + file;
    cp(file, in);
}

template<typename... Args> void ex(const Args&... args) {
    sys(bin, conc(args...));
}

bool cmp_diff() {
    auto ret = sys(diff_tool, "--no-dereference -r", in, out, nul);
    return ret.empty();
}

// On Windows: Tells <SYMLINK> vs <SYMLINKD> and timestamp, which "diff" does not look at
bool cmp_ls() {
    string ls_in = sys(win ? "dir /s" : "ls -l", in);
    string ls_out = sys(win ? "dir /s" : "ls -l", out);

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
    if(win) {
        return cmp_diff() && cmp_ls();
    }
    else {
        return cmp_diff();    
    }
}

}

TEST_CASE("simple backup, diff backup and restore") {
    clean();
    pick("a");
    ex("-m1",in, full);
    ex("-R", full, out);
    cmp();

    ex("-D", in, full, diff);
    rm(out);
    ex("-RD", full, diff, out);
    cmp();
}

TEST_CASE("simple backup, diff backup and restore, restore destination doesn't exist") {
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
    // Must restore timestamp for various kinds of things:
    md(in + "/d"); // dir
    ld(in + "/link_to_d", in + "/d"); // link to dir
    ld(in + "/link_to_missing_dir", in + "/missing_dir"); // broken link to dir
    pick("a");
    lf(in + "/link_to_a", in + "/a"); // link to file
    lf(in + "/link_to_missing_file", in + "/missing_file"); // broken link to file
    ex("-cv3", in, full);
    ex("-R", full, out);
    cmp();
}