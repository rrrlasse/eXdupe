// SPDX-License-Identifier: MIT
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2025: Lasse Mikkel Reinhold

#include "unicode.h"

extern "C" {
#include "lua/lua-5.4.6/include/lauxlib.h"
#include "lua/lua-5.4.6/include/lua.h"
#include "lua/lua-5.4.6/include/lua.hpp"
#include "lua/lua-5.4.6/include/lualib.h"
}

#include "luawrapper.h"
#include "utilities.hpp"
#include "abort.h"

#include <algorithm>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <regex>
#include <format>
#include <iostream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
    string auto_script;
    string user_script;
    int winargs_count = 0;
    string winargs_string;
    lua_State *L = nullptr;
}

std::string utf8_script();

std::string escape_lua_string(const std::string& input) {
    std::string result = input;
    if(!is_valid_utf8(result)) {
        // String is not utf-8, so just pass basic ASCII to the user and replace anything else with '?'
        for (char& c : result) {
            if ((c < ' ') ||
                (c > '~')) {
                c = '?';
            }
        }            
    }
    return result;
}


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

STRING utf8d(const std::string &str) {
#ifdef _WIN32
    if (str.empty())
        return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
#else
    return str;
#endif
}

std::string utf8e(const STRING &str) {
#ifdef _WIN32
    if (str.empty()) {
        return std::string();
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &str[0], (int)str.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
#else
    return str;
#endif
}


bool execute(STRING user_script2, STRING path2, int type, STRING name2, uint64_t size, STRING ext2, [[maybe_unused]] uint32_t attrib, time_ms_t date, bool top_level, Statusbar& statusbar) {
    user_script = utf8e(user_script2);
    string path = utf8e(remove_delimitor(path2));
    string name = utf8e(name2);
    string ext = utf8e(ext2);


    if(L == nullptr) {
        L = luaL_newstate();
        luaL_openlibs(L);
    
        luaMemFile luaMF;

        if (date < 0) {
            date = 0;
        }

        // clang-format off
        auto_script = "function contains(i, l);\nfor _,v in pairs(i) do;if v == l then;return true;end;end;return false;\nend\n";

    #ifdef _WIN32
        winargs_string = ", FILE_ATTRIBUTE_ARCHIVE, FILE_ATTRIBUTE_COMPRESSED, FILE_ATTRIBUTE_DEVICE, FILE_ATTRIBUTE_DIRECTORY, FILE_ATTRIBUTE_ENCRYPTED, FILE_ATTRIBUTE_HIDDEN, FILE_ATTRIBUTE_NORMAL, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, FILE_ATTRIBUTE_OFFLINE, FILE_ATTRIBUTE_READONLY, FILE_ATTRIBUTE_REPARSE_POINT, FILE_ATTRIBUTE_SPARSE_FILE, FILE_ATTRIBUTE_SYSTEM, FILE_ATTRIBUTE_TEMPORARY, FILE_ATTRIBUTE_VIRTUAL";
        winargs_count = 15;
    #endif
        // clang-format on
        
        std::string str2 = utf8_script() + auto_script + "\nfunction include(is_file, is_link, is_dir, is_arg, path, name, size, ext, time_t_time, year, month, day, hour, min, sec" + winargs_string + ")\ntime = os.date(time_t_time)\n" + user_script + "\nend\n";

        luaMF.text = str2.c_str();
        luaMF.size = strlen(luaMF.text);
        int i = lua_load(L, readMemFile, &luaMF, "Lua filter program", NULL);

        if (i != 0) {
            const char *err = lua_tostring(L, lua_gettop(L));
            abort(i != 0, L("\n=================== Auto generated ======================\n%s\n=================== Your script ========================= \n%s\n\n=================== Lua load-time error message =========\n%s"), utf8d(auto_script).c_str(), utf8d(user_script).c_str(), utf8d(err).c_str());
        }

        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            std::cerr << "Error executing Lua script: " << lua_tostring(L, -1) << std::endl;
            lua_close(L);
            return 1;
        }

    }


    {
#ifdef _WIN32
      //  std::lock_guard<std::recursive_mutex> lg(statusbar.get_screen_mutex());
       // _setmode(_fileno(stderr), _O_TEXT);
        //   UINT old_cp = GetConsoleOutputCP();
        //   SetConsoleOutputCP(65001);
#endif

        lua_getglobal(L, "include");
        lua_pushboolean(L, type == FILE_TYPE);
        lua_pushboolean(L, type == SYMLINK_TYPE);
        lua_pushboolean(L, type == DIR_TYPE);
        lua_pushboolean(L, top_level);
        lua_pushstring(L, escape_lua_string(path).c_str());
        lua_pushstring(L, escape_lua_string(name).c_str());
        lua_pushinteger(L, size);
        lua_pushstring(L, escape_lua_string(ext).c_str());

        lua_pushinteger(L, date);
        tm t = local_time_tm(date);
        int year = t.tm_year + 1900;
        int month = t.tm_mon;
        int day = t.tm_wday;
        int hour = t.tm_hour;
        int min = t.tm_min;
        int sec = t.tm_sec;
        lua_pushinteger(L, year);
        lua_pushinteger(L, month);
        lua_pushinteger(L, day);
        lua_pushinteger(L, hour);
        lua_pushinteger(L, min);
        lua_pushinteger(L, sec);

#ifdef _WIN32
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_ARCHIVE);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_COMPRESSED);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_DEVICE);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_DIRECTORY);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_ENCRYPTED);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_HIDDEN);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_NORMAL);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_OFFLINE);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_READONLY);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_REPARSE_POINT);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_SPARSE_FILE);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_SYSTEM);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_TEMPORARY);
        lua_pushboolean(L, attrib & FILE_ATTRIBUTE_VIRTUAL);
#endif

        // lua_atpanic(L, lua_panic);
        // lua_call(L, 0, 1);

        if (lua_pcall(L, 15 + winargs_count, 1, 0) != LUA_OK) {
            abort(false, L("LUA error: %s"), s2w(lua_tostring(L, -1)).c_str());
            return false;
        }

#ifdef _WIN32
        //   SetConsoleOutputCP(old_cp);
       // _setmode(_fileno(stderr), _O_U8TEXT);
#endif
    }

    bool result = lua_toboolean(L, -1);
    lua_pop(L, 1);

    return result;
}

std::string utf8_script() {
    std::string str = std::string(R"delimiter(

reverse_orig = string.reverse
char_orig = string.char
unicode_oig = string.unicode
gensub_orig = string.gensub
byte_orig = string.byte
find_orig = string.find
match_orig = string.match
gmatch_orig = string.gmatch
gsub_orig = string.gsub
len_orig = string.len
sub_orig = string.sub

-- $Id: utf8.lua 179 2009-04-03 18:10:03Z pasta $
--
-- Provides UTF-8 aware string functions implemented in pure lua:
-- * utf8len(s)
-- * utf8sub(s, i, j)
-- * utf8reverse(s)
-- * utf8char(unicode)
-- * utf8unicode(s, i, j)
-- * utf8gensub(s, sub_len)
-- * utf8find(str, regex, init, plain)
-- * utf8match(str, regex, init)
-- * utf8gmatch(str, regex, all)
-- * utf8gsub(str, regex, repl, limit)
--
-- If utf8data.lua (containing the lower<->upper case mappings) is loaded, these
-- additional functions are available:
-- * utf8upper(s)
-- * utf8lower(s)
--
-- All functions behave as their non UTF-8 aware counterparts with the exception
-- that UTF-8 characters are used instead of bytes for all units.

--[[
Copyright (c) 2006-2007, Kyle Smith
All rights reserved.

Contributors:
    Alimov Stepan

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--]]

-- ABNF from RFC 3629
-- 
-- UTF8-octets = *( UTF8-char )
-- UTF8-char   = UTF8-1 / UTF8-2 / UTF8-3 / UTF8-4
-- UTF8-1      = %x00-7F
-- UTF8-2      = %xC2-DF UTF8-tail
-- UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EC 2( UTF8-tail ) /
--               %xED %x80-9F UTF8-tail / %xEE-EF 2( UTF8-tail )
-- UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F3 3( UTF8-tail ) /
--               %xF4 %x80-8F 2( UTF8-tail )
-- UTF8-tail   = %x80-BF
-- 

local byte    = string.byte
local char    = string.char
local dump    = string.dump
local find    = string.find
local format  = string.format
local gmatch  = string.gmatch
local gsub    = string.gsub
local len     = string.len
local lower   = string.lower
local match   = string.match
local rep     = string.rep
local reverse = string.reverse
local sub     = string.sub
local upper   = string.upper

-- returns the number of bytes used by the UTF-8 character at byte i in s
-- also doubles as a UTF-8 character validator
local function utf8charbytes (s, i)
    -- argument defaults
    i = i or 1

    -- argument checking
    if type(s) ~= "string" then
        error("bad argument #1 to 'utf8charbytes' (string expected, got ".. type(s).. ")")
    end
    if type(i) ~= "number" then
        error("bad argument #2 to 'utf8charbytes' (number expected, got ".. type(i).. ")")
    end

    local c = byte_orig(s, i)

    -- determine bytes needed for character, based on RFC 3629
    -- validate byte 1
    if c > 0 and c <= 127 then
        -- UTF8-1
        return 1

    elseif c >= 194 and c <= 223 then
        -- UTF8-2
        local c2 = byte_orig(s, i + 1)

        if not c2 then
            error("UTF-8 string terminated early")
        end

        -- validate byte 2
        if c2 < 128 or c2 > 191 then
            error("Invalid UTF-8 character")
        end

        return 2

    elseif c >= 224 and c <= 239 then
        -- UTF8-3
        local c2 = byte_orig(s, i + 1)
        local c3 = byte_orig(s, i + 2)

        if not c2 or not c3 then
            error("UTF-8 string terminated early")
        end

        -- validate byte 2
        if c == 224 and (c2 < 160 or c2 > 191) then
            error("Invalid UTF-8 character")
        elseif c == 237 and (c2 < 128 or c2 > 159) then
            error("Invalid UTF-8 character")
        elseif c2 < 128 or c2 > 191 then
            error("Invalid UTF-8 character")
        end

        -- validate byte 3
        if c3 < 128 or c3 > 191 then
            error("Invalid UTF-8 character")
        end

        return 3

    elseif c >= 240 and c <= 244 then
        -- UTF8-4
        local c2 = byte_orig(s, i + 1)
        local c3 = byte_orig(s, i + 2)
        local c4 = byte_orig(s, i + 3)

        if not c2 or not c3 or not c4 then
            error("UTF-8 string terminated early")
        end

        -- validate byte 2
        if c == 240 and (c2 < 144 or c2 > 191) then
            error("Invalid UTF-8 character")
        elseif c == 244 and (c2 < 128 or c2 > 143) then
            error("Invalid UTF-8 character")
        elseif c2 < 128 or c2 > 191 then
            error("Invalid UTF-8 character")
        end
        
        -- validate byte 3
        if c3 < 128 or c3 > 191 then
            error("Invalid UTF-8 character")
        end

        -- validate byte 4
        if c4 < 128 or c4 > 191 then
            error("Invalid UTF-8 character")
        end

        return 4

    else
        error("Invalid UTF-8 character")
    end
end

-- returns the number of characters in a UTF-8 string
local function utf8len (s)
    -- argument checking
    if type(s) ~= "string" then
        for k,v in pairs(s) do print('"',tostring(k),'"',tostring(v),'"') end
        error("bad argument #1 to 'utf8len' (string expected, got ".. type(s).. ")")
    end

    local pos = 1
    local bytes = len_orig(s)
    local len = 0

    while pos <= bytes do
        len = len + 1
        pos = pos + utf8charbytes(s, pos)
    end

    return len
end

-- functions identically to string.sub except that i and j are UTF-8 characters
-- instead of bytes
local function utf8sub (s, i, j)
    -- argument defaults
    j = j or -1

    local pos = 1
    local bytes = len_orig(s)
    local len = 0

    -- only set l if i or j is negative
    local l = (i >= 0 and j >= 0) or utf8len(s)
    local startChar = (i >= 0) and i or l + i + 1
    local endChar   = (j >= 0) and j or l + j + 1

    -- can't have start before end!
    if startChar > endChar then
        return ""
    end

    -- byte offsets to pass to string.sub
    local startByte,endByte = 1,bytes
    
    while pos <= bytes do
        len = len + 1

        if len == startChar then
            startByte = pos
        end

        pos = pos + utf8charbytes(s, pos)

        if len == endChar then
            endByte = pos - 1
            break
        end
    end
    
    if startChar > len then startByte = bytes+1   end
    if endChar   < 1   then endByte   = 0         end
    
    return sub_orig(s, startByte, endByte)
end


-- replace UTF-8 characters based on a mapping table
local function utf8replace (s, mapping)
    -- argument checking
    if type(s) ~= "string" then
        error("bad argument #1 to 'utf8replace' (string expected, got ".. type(s).. ")")
    end
    if type(mapping) ~= "table" then
        error("bad argument #2 to 'utf8replace' (table expected, got ".. type(mapping).. ")")
    end

    local pos = 1
    local bytes = len_orig(s)
    local charbytes
    local newstr = ""

    while pos <= bytes do
        charbytes = utf8charbytes(s, pos)
        local c = sub_orig(s, pos, pos + charbytes - 1)

        newstr = newstr .. (mapping[c] or c)

        pos = pos + charbytes
    end

    return newstr
end


-- identical to string.upper except it knows about unicode simple case conversions
local function utf8upper (s)
    return utf8replace(s, utf8_lc_uc)
end

-- identical to string.lower except it knows about unicode simple case conversions
local function utf8lower (s)
    return utf8replace(s, utf8_uc_lc)
end

-- identical to string.reverse except that it supports UTF-8
local function utf8reverse (s)
    -- argument checking
    if type(s) ~= "string" then
        error("bad argument #1 to 'utf8reverse' (string expected, got ".. type(s).. ")")
    end

    local bytes = len_orig(s)
    local pos = bytes
    local charbytes
    local newstr = ""

    while pos > 0 do
        c = byte_orig(s, pos)
        while c >= 128 and c <= 191 do
            pos = pos - 1
            c = byte_orig(s, pos)
        end

        charbytes = utf8charbytes(s, pos)

        newstr = newstr .. sub_orig(s, pos, pos + charbytes - 1)

        pos = pos - 1
    end

    return newstr
end

-- http://en.wikipedia.org/wiki/Utf8
-- http://developer.coronalabs.com/code/utf-8-conversion-utility
local function utf8char(unicode)
    if unicode <= 0x7F then return char_orig(unicode) end
    
    if (unicode <= 0x7FF) then
        local Byte0 = 0xC0 + math.floor(unicode / 0x40);
        local Byte1 = 0x80 + (unicode % 0x40);
        return char_orig(Byte0, Byte1);
    end;
    
    if (unicode <= 0xFFFF) then
        local Byte0 = 0xE0 +  math.floor(unicode / 0x1000);
        local Byte1 = 0x80 + (math.floor(unicode / 0x40) % 0x40);
        local Byte2 = 0x80 + (unicode % 0x40);
        return char_orig(Byte0, Byte1, Byte2);
    end;
    
    if (unicode <= 0x10FFFF) then
        local code = unicode
        local Byte3= 0x80 + (code % 0x40);
        code       = math.floor(code / 0x40)
        local Byte2= 0x80 + (code % 0x40);
        code       = math.floor(code / 0x40)
        local Byte1= 0x80 + (code % 0x40);
        code       = math.floor(code / 0x40)  
        local Byte0= 0xF0 + code;
        
        return char_orig(Byte0, Byte1, Byte2, Byte3);
    end;
    
    error 'Unicode cannot be greater than U+10FFFF!'
end

local shift_6  = 2^6
local shift_12 = 2^12
local shift_18 = 2^18

)delimiter") + std::string(R"delimiter(

local utf8unicode
utf8unicode = function(str, i, j, byte_pos)
    i = i or 1
    j = j or i
    
    if i > j then return end
    
    local char,bytes
    
    if byte_pos then 
        bytes = utf8charbytes(str,byte_pos)
        char  = sub_orig(str,byte_pos,byte_pos-1+bytes)
    else
        char,byte_pos = utf8sub(str,i,i), 0
        bytes         = #char
    end
    
    local unicode
    
    if bytes == 1 then unicode = byte_orig(char) end
    if bytes == 2 then
        local byte0,byte1 = byte_orig(char,1,2)
        local code0,code1 = byte0-0xC0,byte1-0x80
        unicode = code0*shift_6 + code1
    end
    if bytes == 3 then
        local byte0,byte1,byte2 = byte_orig(char,1,3)
        local code0,code1,code2 = byte0-0xE0,byte1-0x80,byte2-0x80
        unicode = code0*shift_12 + code1*shift_6 + code2
    end
    if bytes == 4 then
        local byte0,byte1,byte2,byte3 = byte_orig(char,1,4)
        local code0,code1,code2,code3 = byte0-0xF0,byte1-0x80,byte2-0x80,byte3-0x80
        unicode = code0*shift_18 + code1*shift_12 + code2*shift_6 + code3
    end
    
    return unicode,utf8unicode(str, i+1, j, byte_pos+bytes)
end

-- Returns an iterator which returns the next substring and its byte interval
local function utf8gensub(str, sub_len)
    sub_len        = sub_len or 1
    local byte_pos = 1
    local len      = #str
    return function(skip)
        if skip then byte_pos = byte_pos + skip end
        local char_count = 0
        local start      = byte_pos
        repeat
            if byte_pos > len then return end
            char_count  = char_count + 1
            local bytes = utf8charbytes(str,byte_pos)
            byte_pos    = byte_pos+bytes
            
        until char_count == sub_len
        
        local last  = byte_pos-1
        local sub   = sub_orig(str,start,last)
        return sub, start, last
    end
end

local function binsearch(sortedTable, item, comp)
    local head, tail = 1, #sortedTable
    local mid = math.floor((head + tail)/2)
    if not comp then
        while (tail - head) > 1 do
            if sortedTable[tonumber(mid)] > item then
                tail = mid
            else
                head = mid
            end
            mid = math.floor((head + tail)/2)
        end
    else
    end
    if sortedTable[tonumber(head)] == item then
        return true, tonumber(head)
    elseif sortedTable[tonumber(tail)] == item then
        return true, tonumber(tail)
    else
        return false
    end
end
local function classMatchGenerator(class, plain)
    local codes = {}
    local ranges = {}
    local ignore = false
    local range = false
    local firstletter = true
    local unmatch = false
    
    local it = utf8gensub(class) 
    
    local skip
    for c,bs,be in it do
        skip = be
        if not ignore and not plain then
            if c == "%" then
                ignore = true
            elseif c == "-" then
                table.insert(codes, utf8unicode(c))
                range = true
            elseif c == "^" then
                if not firstletter then
                    error('!!!')
                else
                    unmatch = true
                end
            elseif c == ']' then
                break
            else
                if not range then
                    table.insert(codes, utf8unicode(c))
                else
                    table.remove(codes) -- removing '-'
                    table.insert(ranges, {table.remove(codes), utf8unicode(c)})
                    range = false
                end
            end
        elseif ignore and not plain then
            if c == 'a' then -- %a: represents all letters. (ONLY ASCII)
                table.insert(ranges, {65, 90}) -- A - Z
                table.insert(ranges, {97, 122}) -- a - z
            elseif c == 'c' then -- %c: represents all control characters.
                table.insert(ranges, {0, 31})
                table.insert(codes, 127)
            elseif c == 'd' then -- %d: represents all digits.
                table.insert(ranges, {48, 57}) -- 0 - 9
            elseif c == 'g' then -- %g: represents all printable characters except space.
                table.insert(ranges, {1, 8})
                table.insert(ranges, {14, 31})
                table.insert(ranges, {33, 132})
                table.insert(ranges, {134, 159})
                table.insert(ranges, {161, 5759})
                table.insert(ranges, {5761, 8191})
                table.insert(ranges, {8203, 8231})
                table.insert(ranges, {8234, 8238})
                table.insert(ranges, {8240, 8286})
                table.insert(ranges, {8288, 12287})
            elseif c == 'l' then -- %l: represents all lowercase letters. (ONLY ASCII)
                table.insert(ranges, {97, 122}) -- a - z
            elseif c == 'p' then -- %p: represents all punctuation characters. (ONLY ASCII)
                table.insert(ranges, {33, 47})
                table.insert(ranges, {58, 64})
                table.insert(ranges, {91, 96})
                table.insert(ranges, {123, 126})
            elseif c == 's' then -- %s: represents all space characters.
                table.insert(ranges, {9, 13})
                table.insert(codes, 32)
                table.insert(codes, 133)
                table.insert(codes, 160)
                table.insert(codes, 5760)
                table.insert(ranges, {8192, 8202})
                table.insert(codes, 8232)
                table.insert(codes, 8233)
                table.insert(codes, 8239)
                table.insert(codes, 8287)
                table.insert(codes, 12288)
            elseif c == 'u' then -- %u: represents all uppercase letters. (ONLY ASCII)
                table.insert(ranges, {65, 90}) -- A - Z
            elseif c == 'w' then -- %w: represents all alphanumeric characters. (ONLY ASCII)
                table.insert(ranges, {48, 57}) -- 0 - 9
                table.insert(ranges, {65, 90}) -- A - Z
                table.insert(ranges, {97, 122}) -- a - z
            elseif c == 'x' then -- %x: represents all hexadecimal digits.
                table.insert(ranges, {48, 57}) -- 0 - 9
                table.insert(ranges, {65, 70}) -- A - F
                table.insert(ranges, {97, 102}) -- a - f
            else
                if not range then
                    table.insert(codes, utf8unicode(c))
                else
                    table.remove(codes) -- removing '-'
                    table.insert(ranges, {table.remove(codes), utf8unicode(c)})
                    range = false
                end
            end
            ignore = false
        else
            if not range then
                table.insert(codes, utf8unicode(c))
            else
                table.remove(codes) -- removing '-'
                table.insert(ranges, {table.remove(codes), utf8unicode(c)})
                range = false
            end
            ignore = false
        end
        
        firstletter = false
    end
    
    table.sort(codes)
    
    local function inRanges(charCode)
        for _,r in ipairs(ranges) do
            if r[1] <= charCode and charCode <= r[2] then
                return true
            end
        end
        return false
    end
    if not unmatch then 
        return function(charCode)
            return binsearch(codes, charCode) or inRanges(charCode) 
        end, skip
    else
        return function(charCode)
            return charCode ~= -1 and not (binsearch(codes, charCode) or inRanges(charCode))
        end, skip
    end
end

-- utf8sub with extra argument, and extra result value 
local function utf8subWithBytes (s, i, j, sb)
    -- argument defaults
    j = j or -1

    local pos = sb or 1
    local bytes = len_orig(s)
    local len = 0

    -- only set l if i or j is negative
    local l = (i >= 0 and j >= 0) or utf8len(s)
    local startChar = (i >= 0) and i or l + i + 1
    local endChar   = (j >= 0) and j or l + j + 1

    -- can't have start before end!
    if startChar > endChar then
        return ""
    end

    -- byte offsets to pass to string.sub
    local startByte,endByte = 1,bytes
    
    while pos <= bytes do
        len = len + 1

        if len == startChar then
            startByte = pos
        end

        pos = pos + utf8charbytes(s, pos)

        if len == endChar then
            endByte = pos - 1
            break
        end
    end
    
    if startChar > len then startByte = bytes+1   end
    if endChar   < 1   then endByte   = 0         end
    
    return sub_orig(s, startByte, endByte), endByte + 1
end

local cache = setmetatable({},{
    __mode = 'kv'
})
local cachePlain = setmetatable({},{
    __mode = 'kv'
})
local function matcherGenerator(regex, plain)
    local matcher = {
        functions = {},
        captures = {}
    }
    if not plain then
        cache[regex] =  matcher
    else
        cachePlain[regex] = matcher
    end
    local function simple(func)
        return function(cC) 
            if func(cC) then
                matcher:nextFunc()
                matcher:nextStr()
            else
                matcher:reset()
            end
        end
    end
    local function star(func)
        return function(cC)
            if func(cC) then
                matcher:fullResetOnNextFunc()
                matcher:nextStr()
            else
                matcher:nextFunc()
            end
        end
    end
    local function minus(func)
        return function(cC)
            if func(cC) then
                matcher:fullResetOnNextStr()
            end
            matcher:nextFunc()
        end
    end
    local function question(func)
        return function(cC)
            if func(cC) then
                matcher:fullResetOnNextFunc()
                matcher:nextStr()
            end
            matcher:nextFunc()
        end
    end
    
    local function capture(id)
        return function(cC)
            local l = matcher.captures[id][2] - matcher.captures[id][1]
            local captured = utf8sub(matcher.string, matcher.captures[id][1], matcher.captures[id][2])
            local check = utf8sub(matcher.string, matcher.str, matcher.str + l)
            if captured == check then
                for i = 0, l do
                    matcher:nextStr()
                end
                matcher:nextFunc()
            else
                matcher:reset()
            end
        end
    end
    local function captureStart(id)
        return function(cC)
            matcher.captures[id][1] = matcher.str
            matcher:nextFunc()
        end
    end
    local function captureStop(id)
        return function(cC)
            matcher.captures[id][2] = matcher.str - 1
            matcher:nextFunc()
        end
    end
    
    local function balancer(str)
        local sum = 0
        local bc, ec = utf8sub(str, 1, 1), utf8sub(str, 2, 2)
        local skip = len_orig(bc) + len_orig(ec)
        bc, ec = utf8unicode(bc), utf8unicode(ec)
        return function(cC)
            if cC == ec and sum > 0 then
                sum = sum - 1
                if sum == 0 then
                    matcher:nextFunc()
                end
                matcher:nextStr()
            elseif cC == bc then
                sum = sum + 1
                matcher:nextStr()
            else
                if sum == 0 or cC == -1 then
                    sum = 0
                    matcher:reset()
                else
                    matcher:nextStr()
                end
            end
        end, skip
    end
    
)delimiter") + std::string(R"delimiter(

    matcher.functions[1] = function(cC)
        matcher:fullResetOnNextStr()
        matcher.seqStart = matcher.str
        matcher:nextFunc()
        if (matcher.str > matcher.startStr and matcher.fromStart) or matcher.str >= matcher.stringLen then
            matcher.stop = true
            matcher.seqStart = nil
        end
    end
    
    local lastFunc
    local ignore = false
    local skip = nil
    local it = (function()
        local gen = utf8gensub(regex)
        return function()
            return gen(skip)
        end
    end)()
    local cs = {}
    for c, bs, be in it do
        skip = nil
        if plain then
            table.insert(matcher.functions, simple(classMatchGenerator(c, plain)))
        else
            if ignore then
                if find_orig('123456789', c, 1, true) then
                    if lastFunc then
                        table.insert(matcher.functions, simple(lastFunc))
                        lastFunc = nil
                    end
                    table.insert(matcher.functions, capture(tonumber(c)))
                elseif c == 'b' then
                    if lastFunc then
                        table.insert(matcher.functions, simple(lastFunc))
                        lastFunc = nil
                    end
                    local b
                    b, skip = balancer(sub_orig(regex, be + 1, be + 9))
                    table.insert(matcher.functions, b)
                else
                    lastFunc = classMatchGenerator('%' .. c)
                end
                ignore = false
            else
                if c == '*' then
                    if lastFunc then
                        table.insert(matcher.functions, star(lastFunc))
                        lastFunc = nil
                    else
                        error('invalid regex after ' .. sub_orig(regex, 1, bs))
                    end
                elseif c == '+' then
                    if lastFunc then
                        table.insert(matcher.functions, simple(lastFunc))
                        table.insert(matcher.functions, star(lastFunc))
                        lastFunc = nil
                    else
                        error('invalid regex after ' .. sub_orig(regex, 1, bs))
                    end
                elseif c == '-' then
                    if lastFunc then
                        table.insert(matcher.functions, minus(lastFunc))
                        lastFunc = nil
                    else
                        error('invalid regex after ' .. sub_orig(regex, 1, bs))
                    end
                elseif c == '?' then
                    if lastFunc then
                        table.insert(matcher.functions, question(lastFunc))
                        lastFunc = nil
                    else
                        error('invalid regex after ' .. sub_orig(regex, 1, bs))
                    end
                elseif c == '^' then
                    if bs == 1 then
                        matcher.fromStart = true
                    else
                        error('invalid regex after ' .. sub_orig(regex, 1, bs))
                    end
                elseif c == '$' then
                    if be == len_orig(regex) then
                        matcher.toEnd = true
                    else
                        error('invalid regex after ' .. sub_orig(regex, 1, bs))
                    end
                elseif c == '[' then
                    if lastFunc then
                        table.insert(matcher.functions, simple(lastFunc))
                    end
                    lastFunc, skip = classMatchGenerator(sub_orig(regex, be + 1))
                elseif c == '(' then
                    if lastFunc then
                        table.insert(matcher.functions, simple(lastFunc))
                        lastFunc = nil
                    end
                    table.insert(matcher.captures, {})
                    table.insert(cs, #matcher.captures)
                    table.insert(matcher.functions, captureStart(cs[#cs]))
                    if sub_orig(regex, be + 1, be + 1) == ')' then matcher.captures[#matcher.captures].empty = true end
                elseif c == ')' then
                    if lastFunc then
                        table.insert(matcher.functions, simple(lastFunc))
                        lastFunc = nil
                    end
                    local cap = table.remove(cs)
                    if not cap then
                        error('invalid capture: "(" missing')
                    end
                    table.insert(matcher.functions, captureStop(cap))
                elseif c == '.' then
                    if lastFunc then
                        table.insert(matcher.functions, simple(lastFunc))
                    end
                    lastFunc = function(cC) return cC ~= -1 end
                elseif c == '%' then
                    ignore = true
                else
                    if lastFunc then
                        table.insert(matcher.functions, simple(lastFunc))
                    end
                    lastFunc = classMatchGenerator(c)
                end
            end
        end
    end
    if #cs > 0 then
        error('invalid capture: ")" missing')
    end
    if lastFunc then
        table.insert(matcher.functions, simple(lastFunc))
    end
    lastFunc = nil
    ignore = nil
    
    table.insert(matcher.functions, function()
        if matcher.toEnd and matcher.str ~= matcher.stringLen then
            matcher:reset()
        else
            matcher.stop = true
        end
    end)
    
    matcher.nextFunc = function(self)
        self.func = self.func + 1
    end
    matcher.nextStr = function(self)
        self.str = self.str + 1
    end
    matcher.strReset = function(self)
        local oldReset = self.reset
        local str = self.str
        self.reset = function(s)
            s.str = str
            s.reset = oldReset
        end
    end
    matcher.fullResetOnNextFunc = function(self)
        local oldReset = self.reset
        local func = self.func +1
        local str = self.str
        self.reset = function(s)
            s.func = func
            s.str = str
            s.reset = oldReset
        end
    end
    matcher.fullResetOnNextStr = function(self)
        local oldReset = self.reset
        local str = self.str + 1
        local func = self.func
        self.reset = function(s)
            s.func = func
            s.str = str
            s.reset = oldReset
        end
    end
    
    matcher.process = function(self, str, start)
        
        self.func = 1
        start = start or 1
        self.startStr = (start >= 0) and start or utf8len(str) + start + 1
        self.seqStart = self.startStr
        self.str = self.startStr
        self.stringLen = utf8len(str) + 1
        self.string = str
        self.stop = false
        
        self.reset = function(s)
            s.func = 1
        end

        local lastPos = self.str
        local lastByte
        local char
        while not self.stop do
            if self.str < self.stringLen then
                --[[ if lastPos < self.str then
                    print('last byte', lastByte)
                    char, lastByte = utf8subWithBytes(str, 1, self.str - lastPos - 1, lastByte)
                    char, lastByte = utf8subWithBytes(str, 1, 1, lastByte)
                    lastByte = lastByte - 1
                else
                    char, lastByte = utf8subWithBytes(str, self.str, self.str)
                end
                lastPos = self.str ]]
                char = utf8sub(str, self.str,self.str)
                --print('char', char, utf8unicode(char))
                self.functions[self.func](utf8unicode(char))
            else
                self.functions[self.func](-1)
            end
        end
        
        if self.seqStart then
            local captures = {}
            for _,pair in pairs(self.captures) do
                if pair.empty then
                    table.insert(captures, pair[1])
                else
                    table.insert(captures, utf8sub(str, pair[1], pair[2]))
                end
            end
            return self.seqStart, self.str - 1, table.unpack(captures)
        end
    end
    
    return matcher
end

-- string.find
local function utf8find(str, regex, init, plain)
    local matcher = cache[regex] or matcherGenerator(regex, plain)
    return matcher:process(str, init)
end

-- string.match
local function utf8match(str, regex, init)
    init = init or 1
    local found = {utf8find(str, regex, init)}
    if found[1] then
        if found[3] then
            return table.unpack(found, 3)
        end
        return utf8sub(str, found[1], found[2])
    end
end

-- string.gmatch
local function utf8gmatch(str, regex, all)
    regex = (utf8sub(regex,1,1) ~= '^') and regex or '%' .. regex 
    local lastChar = 1
    return function()
        local found = {utf8find(str, regex, lastChar)}
        if found[1] then
            lastChar = found[2] + 1
            if found[all and 1 or 3] then
                return table.unpack(found, all and 1 or 3)
            end
            return utf8sub(str, found[1], found[2])
        end
    end
end

local function replace(repl, args)
    local ret = ''
    if type(repl) == 'string' then
        local ignore = false
        local num = 0
        for c in utf8gensub(repl) do
            if not ignore then
                if c == '%' then
                    ignore = true
                else
                    ret = ret .. c
                end
            else
                num = tonumber(c)
                if num then
                    ret = ret .. args[num]
                else
                    ret = ret .. c
                end
                ignore = false
            end
        end
    elseif type(repl) == 'table' then
        ret = repl[args[1] or args[0]] or ''
    elseif type(repl) == 'function' then
        if #args > 0 then
            ret = repl(table.unpack(args, 1)) or ''
        else
            ret = repl(args[0]) or ''
        end
    end
    return ret
end
-- string.gsub
local function utf8gsub(str, regex, repl, limit)
    limit = limit or -1
    local ret = ''
    local prevEnd = 1
    local it = utf8gmatch(str, regex, true)
    local found = {it()}
    local n = 0
    while #found > 0 and limit ~= n do
        local args = {[0] = utf8sub(str, found[1], found[2]), table.unpack(found, 3)}
        ret = ret .. utf8sub(str, prevEnd, found[1] - 1)
        .. replace(repl, args)
        prevEnd = found[2] + 1
        n = n + 1 
        found = {it()}
    end
    return ret .. utf8sub(str, prevEnd), n 
end

local utf8 = {}                                                                                             

string.reverse = utf8reverse
string.char = utf8char
string.unicode = utf8unicode
string.gensub = utf8gensub
string.byte = utf8unicode
string.find    = utf8find
string.match   = utf8match
string.gmatch  = utf8gmatch
string.gsub    = utf8gsub  
string.len = utf8len
string.sub = utf8sub

reverse = utf8reverse
char = utf8char
unicode = utf8unicode
gensub = utf8gensub
byte = utf8unicode
find    = utf8find
match   = utf8match
gmatch  = utf8gmatch
gsub    = utf8gsub  
len = utf8len
sub = utf8sub

)delimiter");

    return str;
}
