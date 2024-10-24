module;

#include <string>
#include <vector>

#include "utilities.hpp"

export module FileTypes;

export class FileTypes
{
public:
    FileTypes() {
        add({
            L("jpg"), L("mp4"), L("mpeg"), L("wmv"), L("jpeg"),
            L("png"), L("avi"), L("mov"), L("flv"), L("aac"),
            L("ogg"), L("mp3"), L("webp"), L("mkv") });
    }

    void add(const std::vector<STRING>& extensions) {
        for (auto e : extensions) {
#ifdef _WIN32
            e = lcase(e);
#endif
            types.push_back({ L(".") + e, {}, {} });
        }
    }

    bool high_entropy(FILE*, const STRING& filename) {
#ifdef _WIN32
        STRING f = lcase(filename);
#else
        const STRING& f = filename;
#endif

        for (auto& t : types) {
            if (f.ends_with(t.extension)) {
                return true;
            }
        }
        return false;
    }

    struct t {
        STRING extension;
        uint64_t offset;
        std::string data;
    };

    std::vector<t> types;

};

