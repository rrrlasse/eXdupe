// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include "unicode.h"

extern "C" {
#include "lua/lapi.h"
#include "lua/lauxlib.h"
#include "lua/lua.h"
#include "lua/lualib.h"
}

#include "luawrapper.h"
#include "utilities.hpp"
#include <algorithm>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <vector>

typedef struct luaMemFile {
    const char *text;
    size_t size;
} luaMemFile;

const char *readMemFile(lua_State *, void *ud, size_t *size) {
    luaMemFile *luaMF = (luaMemFile *)ud;
    if (luaMF->size == 0) {
        return NULL;
    }
    *size = luaMF->size;
    luaMF->size = 0;
    return luaMF->text;
}

bool execute(STRING script2, STRING dir2, STRING file2, STRING name2, uint64_t size, STRING ext2, uint32_t attrib, tm *date) {
    string script = wstring2string(script2);
    string dir = wstring2string(dir2);
    string name = wstring2string(name2);
    string ext = wstring2string(ext2);
    string file = wstring2string(file2);

    // Open the LUA state
    lua_State *L = luaL_newstate();

    luaL_openlibs(L);
    luaMemFile luaMF;

    dir = wstring2string(remove_delimitor(string2wstring(dir)));
    file = wstring2string(remove_delimitor(string2wstring(file)));

    myReplaceSTR(dir, "\\", "\\\\");
    myReplaceSTR(file, "\\", "\\\\");

    date->tm_yday = 0;
    date->tm_isdst = -1;

    string s =
        "dir = " + (dir == "" ? "ni" : "\"" + dir + "\"") + "\n" + "file = " + (file == "" ? "ni" : "\"" + file + "\"") + "\n" +
        "name = " + (name == "" ? "ni" : "\"" + name + "\"") + "\n" + "size = " + wstring2string(str(size)) + "\n" +
        "ext = " + (file == "" ? "ni" : "\"" + ext + "\"") + "\n" + "date = os.time{year=" + wstring2string(str(date->tm_year)) +
        ", month=" + wstring2string(str(date->tm_mon)) + ", day=" + wstring2string(str(date->tm_mday)) + ", hour=" + wstring2string(str(date->tm_hour)) +
        ", min=" + wstring2string(str(date->tm_min)) + ", sec=" + wstring2string(str(date->tm_min)) + "}" + "\n\n" +

#ifdef WINDOWS
        "ARCHIVE = " + (attrib & FILE_ATTRIBUTE_ARCHIVE ? "true" : "false") + "\n" + "COMPRESSED = " + (attrib & FILE_ATTRIBUTE_COMPRESSED ? "true" : "false") +
        "\n" + "DEVICE = " + (attrib & FILE_ATTRIBUTE_DEVICE ? "true" : "false") + "\n" +
        "DIRECTORY = " + (attrib & FILE_ATTRIBUTE_DIRECTORY ? "true" : "false") + "\n" +
        "ENCRYPTED = " + (attrib & FILE_ATTRIBUTE_ENCRYPTED ? "true" : "false") + "\n" + "HIDDEN = " + (attrib & FILE_ATTRIBUTE_HIDDEN ? "true" : "false") +
        "\n" + "NORMAL = " + (attrib & FILE_ATTRIBUTE_NORMAL ? "true" : "false") + "\n" +
        "NOT_CONTENT_INDEXED = " + (attrib & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED ? "true" : "false") + "\n" +
        "OFFLINE = " + (attrib & FILE_ATTRIBUTE_OFFLINE ? "true" : "false") + "\n" + "READONLY = " + (attrib & FILE_ATTRIBUTE_READONLY ? "true" : "false") +
        "\n" + "REPARSE_POINT = " + (attrib & FILE_ATTRIBUTE_REPARSE_POINT ? "true" : "false") + "\n" +
        "SPARSE_FILE = " + (attrib & FILE_ATTRIBUTE_SPARSE_FILE ? "true" : "false") + "\n" + "SYSTEM = " + (attrib & FILE_ATTRIBUTE_SYSTEM ? "true" : "false") +
        "\n" + "TEMPORARY = " + (attrib & FILE_ATTRIBUTE_TEMPORARY ? "true" : "false") + "\n" +
        "VIRTUAL = " + (attrib & FILE_ATTRIBUTE_VIRTUAL ? "true" : "false") + "\n\n" +
#endif

        "function contains(items, item)\n" + "for _,v in pairs(items) do\n" + "  if v == item then\n" + "    return true\n" + "  end\n" + "end\n" +
        "return false\n" + "end\n\n" + script;

    luaMF.text = s.c_str();
    luaMF.size = strlen(luaMF.text);

    int i = lua_load(L, readMemFile, &luaMF, "Lua filter program", NULL);

    if (i != 0) {
        const char *err = lua_tostring(L, lua_gettop(L));

        abort(i != 0,
              UNITXT("%s\n--------------------------\n%s\n---------------------"
                     "-----\n"),
              string2wstring(string(err)).c_str(), string2wstring(s).c_str());
    }

    lua_call(L, 0, 1);

    // There was no error
    // Let's get the result from the stack
    bool result = (bool)lua_toboolean(L, lua_gettop(L));
    lua_pop(L, 1);
    lua_close(L);
    return result;
}
