module;

#include "utilities.hpp"
#include "contents_t.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <assert.h>
#include <optional>

export module UntouchedFiles;

export class UntouchedFiles
{
public:
    // todo, remove name_part and extract it from fullpath
	optional<contents_t> exists(STRING fullpath, STRING name_part, pair<time_ms_t, time_ms_t>& t) {
        auto it = contents_full.find(CASESENSE(abs_path(fullpath)));
        // The "it->second.name == filename" is for Windows where we decide to do a full backup of a file even if its only change was a case-rename. Note that
        // drive-letter casing can apparently fluctuate randomly on Windows, so don't compare full paths
        bool ret = (it != contents_full.end() && it->second.file_c_time == t.first && it->second.file_modified == t.second && it->second.name == name_part);
        if(ret) {
            return it->second;
        }
        return {};
    }

    void add_during_backup(contents_t c) {
        contents_full[CASESENSE(abs_path(c.abs_path))] = c;
    }

    void add_during_restore(contents_t c) {
        rassert(!(contents_full.find(CASESENSE(c.abs_path)) != contents_full.end()), "");
        contents_full[CASESENSE(c.abs_path)] = c;
        rassert(!(contents_full_ids.find(c.file_id) != contents_full_ids.end()), "");
        contents_full_ids[c.file_id] = CASESENSE(c.abs_path);
    }

    void initialize_if_untouched(contents_t& c) {
        if(c.unchanged) {
            auto id_iter = contents_full_ids.find(c.file_id);
            rassert(id_iter != contents_full_ids.end(), "");
            auto p = id_iter->second;
            auto path_iter = contents_full.find(p);
            rassert(path_iter != contents_full.end(), "");
            // todo, maybe it's better to remove this and let the payload writer access contents_full directly                
            c = path_iter->second;
            c.unchanged = true;
        }
    }

private:
	unordered_map<STRING, contents_t> contents_full; // {abs_path, contents}
	unordered_map<uint64_t, STRING> contents_full_ids; // {file_id, abs_path}
};

