module;

#include "utilities.hpp"

export module FileTypes;

export class FileTypes
{
public:
	FileTypes() {
		add({
			UNITXT("jpg"), UNITXT("mp4"), UNITXT("mpeg"), UNITXT("wmv"), UNITXT("jpeg"),
			UNITXT("png"), UNITXT("avi"), UNITXT("mov"), UNITXT("flv"), UNITXT("aac"),
			UNITXT("ogg"), UNITXT("mp3"), UNITXT("webp"), UNITXT("mkv") });
	}

	void add(const std::vector<STRING>& extensions) {
		for (auto e : extensions) {
#ifdef _WIN32
			e = lcase(e);
#endif
			types.push_back({ UNITXT(".") + e, {}, {} });
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

