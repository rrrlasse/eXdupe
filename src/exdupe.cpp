// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2023: Lasse Mikkel Reinhold

#include <string>

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_REVISION 0

const size_t K = 1024;
const size_t M = 1024 * K;
const size_t G = 1024 * M;

// Keep DISK_READ_CHUNK > DEDUPE_LARGE > DEDUPE_SMALL and all power of 2

// Identical data is being searched (deduped) in two different block sizes, one intended for short
// range compression and the other across terabyte ranges. Increase values to improve compression 
// ratio for large data sets; decrease to improve for small data sets (also increase memory_usage 
// to improve for any size).
size_t DEDUPE_SMALL = 4 * K;
size_t DEDUPE_LARGE = 128 * K;

// Data is read from disk and deduplicated in DISK_READ_CHUNK bytes at a time during backup. 
const size_t DISK_READ_CHUNK = 8 * M;

// Restore takes part by resolving a tree structure of backwards references in past data. Resolve
// RESTORE_CHUNKSIZE bytes of payload at a time (too large value can potentially expand a too huge
// tree; too small value can be slow)
const size_t RESTORE_CHUNKSIZE = 128 * K;

// Keep the last RESTORE_BUFFER bytes of resolved data in memory, so that we don't have to seek on 
// the disk while building above mentioned tree
const size_t RESTORE_BUFFER = 512 * M; 

#define compile_assert(x) extern int __dummy[(int)x];

compile_assert(sizeof(size_t) == 8);

#define NOMINMAX
#include "libeXdupe/libeXdupe.h"
#include "unicode.h"
#include <limits>
#include <utility>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <sys/stat.h>
#include "io.hpp"
#include <stdarg.h>
#include "utilities.hpp"
#include <vector>
#include "trex/trex.h"
#include "luawrapper.h"
#include "bytebuffer.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include <stdint.h>

#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64)
	#define WINDOWS
#endif

#ifdef WINDOWS
  const bool WIN = true;
  #include <fcntl.h>
  #include <windows.h>
  #include <io.h>
  #include "shadow/shadow.h"

  #define CURDIR UNITXT(".\\")
  #define DELIM_STR UNITXT("\\")
  #define DELIM_CHAR UNITXT('\\')
  #define LONGLONG UNITXT("%I64d")

  #define CASESENSE(str) lcase(str)

#else

  #define unsnap(x) x

  #define _isatty isatty
  #define _fileno fileno

  const bool WIN = false;

  #include "dirent.h"
  #include <sys/stat.h>
  #include <sys/time.h>
  #include <sys/types.h>
  
  #define CURDIR UNITXT("./")
  #define DELIM_STR UNITXT("/")
  #define DELIM_CHAR '/'
  #define LONGLONG UNITXT("%lld")

  #define CASESENSE(str) str

#endif

using namespace std;

// command line flags
uint64_t memory_usage = 2*G;
bool continue_flag = false;
bool overwrite_flag = false;
bool recursive_flag = true;
bool restore_flag = false;
uint32_t threads = 6;
int flags_exist = 0;
bool diff_flag = false;
bool compress_flag = false;
bool tty_stderr;
bool list_flag = false;
bool named_pipes = false;
bool follow_symlinks = false;
bool gui_interact = false;
bool shadow_copy = false;
bool absolute_path = false;
uint32_t verbose_level = 8;
uint32_t megabyte_flag = 0;
uint32_t gigabyte_flag = 0;
uint32_t threads_flag = 0;
uint32_t compression_level = 1;
size_t input_flag = 0;	

// statistics to show to user
uint64_t files = 0;
uint64_t dirs = 0;
uint64_t tot_res = 0;

STRING full;
STRING diff;
STRING directory;
vector <STRING> inputfiles;
STRING name;
STRING base;
vector <STRING> restorelist; // optional list of individual files/dirs to restore
vector <STRING> excludelist;
STRING lua = UNITXT("");
vector <STRING> shadows;
vector <STRING> messages2;

FILE *ofile = 0, *ifile = 0;

Cio io = Cio();

unsigned char *in, *out;
uint64_t bits;

// various
vector<STRING>argv;
int argc;
STRING flags;

char tmp[1000000 + DISK_READ_CHUNK];
CHR wtmp[1000000 + DISK_READ_CHUNK];

unsigned char extract_concatenate[RESTORE_CHUNKSIZE + 1000000];
unsigned char *extract_in;
unsigned char *extract_out;

#define BLANK_LINE UNITXT("\r                                                                 \r") // 65
#define QLZ_SIZE_OVERHEAD 400

STRING output_file;
bool output_file_mine = false;

enum status_t {BACKUP, DIFF_BACKUP, RESTORE, DIFF_RESTORE1, DIFF_RESTORE2};
status_t status;

void *hashtable;

STRING tempdiff = UNITXT("EXDUPE.TMP");

typedef struct
{
	STRING filename;
	uint64_t offset;
	FILE *handle;
} file_offset_t;

vector <file_offset_t> infiles;

typedef struct
{
	STRING name;
	STRING link;
	uint64_t size;
	uint64_t payload;
	uint64_t checksum;
	tm file_date;
	int attributes;
	bool directory;
	bool symlink;
} contents_t;

vector <contents_t> contents;

typedef struct
{
	uint64_t payload;
	uint64_t archive_offset;
	uint64_t payload_reference;
	size_t length;
	char is_reference;
} reference_t;

vector <reference_t> references;
vector <reference_t> read_ahead;

uint64_t total_decompressed = 0;

STRING left(const STRING &s)
{
	const size_t t = s.find_last_of(UNITXT("/\\"));
	if(t != string::npos)
		return s.substr(0, t);
	else
		return UNITXT("");
}

STRING right(const STRING &s)
{
	const size_t t = s.find_last_of(UNITXT("/\\"));
	if(t != string::npos)
		return s.substr(t + 1);
	else
		return UNITXT("");
}


void update_statusbar(bool force_update, uint64_t counter_payload)
{
	if(verbose_level < 7 || !tty_stderr)
		return;

	static status_t last_stat = BACKUP;
    static uint64_t last_tick = 0, last_speed_tick = 0, speed = 0;
    static uint64_t last_payload_counter_mbs = 0;
    uint64_t t = GetTickCount() - last_tick;
    uint64_t s = GetTickCount() - last_speed_tick;

	if(last_stat != status)
	{
		last_speed_tick = 0;
		last_payload_counter_mbs = 0;
		s = 0;
		last_stat = status;
	}
	
    if (s > 8000)
    {
        // update speed each N'th second
        speed = (counter_payload - last_payload_counter_mbs) / s;
        last_payload_counter_mbs = counter_payload;
        last_speed_tick = GetTickCount();
    }

    if (force_update || t > 200)
    {
        // draw status bar each 0.2 second
        last_tick = GetTickCount();
        if(status == BACKUP || status == DIFF_BACKUP)
		{
			uint64_t into = io.write_count >> 20;
			if(into == 0 && io.write_count > 0)
				into = 1;
            FPRINTF(stderr, UNITXT("Compressed %s MB (%s MB/s) into %s MB     \r"), del(counter_payload >> 20).c_str(), del(speed >> 10).c_str(), del(into).c_str());
		}
        else if(status == RESTORE)
            FPRINTF(stderr, UNITXT("Written %s MB (%s MB/s)   \r"), del(counter_payload >> 20).c_str(), del(speed >> 10).c_str());
        else if(status == DIFF_RESTORE1)
            FPRINTF(stderr, UNITXT("Pass 1 of 2: Written %s MB (%s MB/s)     \r"), del(counter_payload >> 20).c_str(), del(speed >> 10).c_str());
        else if(status == DIFF_RESTORE2)
            FPRINTF(stderr, UNITXT("Pass 2 of 2: Written %s MB (%s MB/s)     \r"), del(counter_payload >> 20).c_str(), del(speed >> 10).c_str());
	}
}

void PRINT_MESSAGES()
{
	bool has_printed = false;
	if(messages2.size() > 0) 
	{
		FPRINTF(stderr, UNITXT("\r%s\r"), BLANK_LINE);
		FPRINTF(stderr, UNITXT("\nRepetition of warnings and notifications, if any exists:\n"));
		for(size_t i = 0; i < messages2.size(); i++)
		{
			if(messages2[i].substr(0, 1) != UNITXT(" ")) // we don't want compressed files and speed shown 
			{
				FPRINTF(stderr, UNITXT("\n | %s"), messages2[i].c_str());
				has_printed = true;
			}
		}
		if(has_printed)
			FPRINTF(stderr, UNITXT("\n"));
	}
}

void abort(bool b, const CHR *fmt, ...)
{
	if(b)
	{
		va_list argv;
		va_start(argv, fmt);
//		set_bold(true);
		FPRINTF(stderr, UNITXT("\r%s\r"), BLANK_LINE);
		FPRINTF(stderr, UNITXT("eXdupe: "));
//		set_bold(false);
		VFPRINTF(stderr, fmt, argv);
		va_end(argv);
		FPRINTF(stderr, UNITXT("\n"));

		if(output_file_mine && output_file != UNITXT("-stdout"))
		{
			if(ofile != 0)
			{
				io.close(ofile);
				ofile = 0;
				REMOVE(output_file.c_str());
			}
		}
#ifdef WINDOWS
		unshadow();
#endif
		exit(1);
	}
}

#define FULL_BACKUP UNITXT("Full backup:\n") \
	UNITXT("   [-vxarctpmgl] [-f] [-s] <sources> <destination>\n") \
	UNITXT("   [-vxarctpmgl] <-stdin> <filename to assign> <destination>\n\n") \
	UNITXT("   <destination> can either be -stdout or a file. Use .full as file extension")
#define DIFFERENTIAL_BACKUP UNITXT("Differential backup:\n") \
	UNITXT("   -D[vxarctpl] [-f] [-s] <sources> <.full file> <destination>\n") \
	UNITXT("   -D[vxarctpl] <-stdin> <filename to assign> <.full file> <destination>\n\n") \
	UNITXT("   <destination> can either be -stdout or a file. Use .diff as file extension")

#define RESTORE_FULL_BACKUP UNITXT("Restore full backup:\n") \
	UNITXT("   -R[vo] <.full file> <destination directory | -stdout> [files]\n") \
	UNITXT("   -R[vo] <-stdin> <destination directory>\n\n") \
	UNITXT("   [files] is one or more files, drives or directories to restore, typed as\n") \
    UNITXT("   provided by -L flag")
//         "================================================================================\n" // 80 chars of UNITXT("="

#define RESTORE_DIFFERENTIAL_BACKUP UNITXT("Restore differential backup:\n") \
	UNITXT("   -RD[vo] <.full> <.diff file> <destination directory | -stdout> [files]\n") \
	UNITXT("   -RD[vo] <-stdin> <.diff file> <destination directory>\n\n") \
	UNITXT("   [files] is one or more files, drives or directories to restore, typed as\n") \
    UNITXT("   provided by -L flag")

#define LIST UNITXT("List contents:\n") \
	UNITXT("   -L <.full file | .diff file>")


//  UNITXT("================================================================================\n" // 80 chars of UNITXT("="


void PRINT(uint32_t verbosity, const CHR *fmt, ...)
{
	va_list argv;
    va_start(argv, fmt);

	if(verbosity > verbose_level)
		return;
	
	VSPRINTF(wtmp, fmt, argv);
	STRING s = STRING(wtmp);

    va_end(argv);


	if(s.size() < 78)
	{
		s = s + STRING(78 - s.size(), ' ');
	}
	FPRINTF(stderr, UNITXT("%s\n"), s.c_str());
	
//	messages2.push_back(s);

	if(gui_interact)
		fflush(stderr);
}

STRING date2str(tm *date)
{
	if(date == 0)
		return UNITXT("");

	CHR dst[1000];
	SPRINTF(dst, UNITXT("%04u-%02u-%02u %02u:%02u"), date->tm_year, date->tm_mon, date->tm_mday, date->tm_hour, date->tm_min);
	return STRING(dst);
}

void write_contents_item(FILE *file, contents_t *c)
{
	io.writestr(c->name, file);
	io.writestr(c->link, file);
	io.write64(c->size, file);
	io.write64(c->payload , file);
	io.write64(c->checksum, file);
	io.write_date(&c->file_date, file);
	io.write32(c->attributes, file);
	io.write8(c->directory ? 1 : 0, file);
	io.write8(c->symlink ? 1 : 0, file);
}


void add_file(const STRING &file, uint64_t offset)
{
	file_offset_t t;
	t.filename = file;
	t.offset = offset;
	t.handle = 0;
	infiles.push_back(t);
}


// **************************************************************************************************************
//
// Following handful of functions are rather complicated. They resolve a tree of backwards-references of data
// chunks. Todo: Some of this may be simplified alot by using existing tree libraries, maybe in STL.
//
// **************************************************************************************************************

uint64_t pay_count = 0;

// todo, change to STL lower_bound
uint64_t belongs_to(uint64_t offset) // todo: verify that this algorithm also works for infiles.size() == 2
{
	// belongs_to requires first element to be 0!
	abort(infiles.size() == 0, UNITXT("infiles.size() == 0"));
	abort(infiles[0].offset != 0, UNITXT("infiles[0].offset != 0"));

	if(offset >= infiles[infiles.size() - 1].offset)
		return infiles.size() - 1;

	uint64_t lower = 0;
	uint64_t upper = infiles.size() - 1;

	while(upper - lower > 1)
	{
		uint64_t middle = lower + (upper - lower) / 2;
		if(offset >= infiles[middle].offset)
			lower = middle;
		else
			upper = middle;
	}
	return lower;
}


	 
void add_references(const unsigned char *src, size_t len, uint64_t archive_offset)
{
	size_t pos = 0;
	reference_t ref;
	uint64_t payload;
	size_t len2 = std::numeric_limits<size_t>::max();

	while(pos < len)
	{
		int r = dup_decompress_simulate(src + pos, &len2, &payload);
		
		ref.length = len2;
		ref.is_reference = static_cast<char>(r);
		ref.payload = pay_count;
		pay_count += len2;

		if(r == 0)
		{
			// raw data chunk
			ref.payload_reference = 0;
			ref.archive_offset = archive_offset + pos; 
		}
		else if(r == 1)
		{
			// reference
			ref.payload_reference = payload;
			ref.archive_offset = 0; 
		}
		else
		{
			abort(true, UNITXT("Internal error, dup_decompress_simulate() = %d"), r);
		}
		references.push_back(ref);
		pos += dup_size_compressed(src + pos);
	}
}

int write_references(FILE *file)
{
	io.try_write("REFERENC", 8, file);
	uint64_t w = io.write_count;
	io.write64(references.size(), file);

	for(size_t i = 0; i < references.size(); i++)
	{
		io.write8(references[i].is_reference, file);
		if(references[i].is_reference)
			io.write64(references[i].payload_reference, file);
		else
			io.write64(references[i].archive_offset, file);

		io.write64(references[i].payload, file);
		io.write32(static_cast<uint32_t>(references[i].length), file);
	}

	io.write32(0, file);
	io.write64(io.write_count - w, file);

	return 0;
}

uint64_t seek_to_header(FILE *file, const string &header)
{
	//  archive   HEADER  data  sizeofdata  HEADER  data  sizeofdata
	uint64_t orig = io.tell(file);
	uint64_t s;
	string h = "";
	int i = io.seek(file, -3, SEEK_END); 
	abort(i != 0, UNITXT("Archive corrupted or on a non-seekable device"));
	io.try_read(tmp, 3, file);
	abort(!equal2(tmp, "END", 3), UNITXT("Unexpected end of archive (header end marker)"));
	io.seek(file, -3, SEEK_END);

	while(h != header)
	{
		abort(io.seek(file, - 8, SEEK_CUR) != 0, UNITXT("Cannot find header '%s'"), header.c_str());
		s = io.read64(file);
		abort(io.seek(file, - 8 - s - 8, SEEK_CUR) != 0, UNITXT("Cannot find header '%s'"), header.c_str());
		memset(tmp, 0, 9);
		io.try_read(tmp, 8, file);
		h = tmp;
		abort(io.seek(file, - 8, SEEK_CUR) != 0, UNITXT("Cannot find header '%s'"), header.c_str());
	}
	io.seek(file, 8, SEEK_CUR);
	return orig;
}

uint64_t read_references(FILE *file, uint64_t base_payload)
{
	uint64_t orig = seek_to_header(file, "REFERENC");
	uint64_t n = io.read64(file);
	uint64_t added_payload = 0;

	for(uint64_t i = 0; i < n; i++)
	{
		reference_t ref;

		ref.is_reference = io.read8(file);
		if(ref.is_reference)
			ref.payload_reference = io.read64(file);
		else
			ref.archive_offset = io.read64(file);
		ref.payload = io.read64(file) + base_payload;
		ref.length = io.read32(file);

		added_payload += ref.length;
		references.push_back(ref);
	}

	io.seek(file, orig, SEEK_SET);
	return added_payload;
}


uint64_t find_reference(uint64_t payload)
{
	uint64_t lower = 0;
	uint64_t upper = references.size() - 1;

	if(references.size() == 0)
		return std::numeric_limits<uint64_t>::max();

	while(upper != lower)
	{
		uint64_t middle = lower + (upper - lower) / 2;

		if(references[middle].payload + references[middle].length - 1 < payload)
			lower = middle + 1;
		else
			upper = middle;
	}

	if(references[lower].payload <= payload && references[lower].payload + references[lower].length - 1 >= payload)
		return lower;
	else
		return std::numeric_limits<uint64_t>::max();
}

bool resolve(uint64_t payload, size_t size, unsigned char *dst, FILE *ifile, FILE *fdiff, uint64_t splitpay)
{
	size_t bytes_resolved = 0;

	while(bytes_resolved < size)
	{
		uint64_t rr = find_reference(payload + bytes_resolved);
		if(rr == std::numeric_limits<uint64_t>::max())
			abort(true, UNITXT("Internal error, find_reference() = -1"));
		uint64_t prior = payload + bytes_resolved - references[rr].payload;
		size_t needed = size - bytes_resolved;
		size_t ref_has = references[rr].length - prior >= needed ? needed : references[rr].length - prior;
		
		if(references[rr].is_reference)
		{
			resolve(references[rr].payload_reference + prior, ref_has, dst + bytes_resolved, ifile, fdiff, splitpay);
		}
		else
		{

			char *b = buffer_find(references[rr].payload, references[rr].length);
			if(b != 0)
			{
				memcpy(dst + bytes_resolved, b + prior, ref_has);
			}
			else
			{
				FILE *f;
				if(references[rr].payload >= splitpay)
					f = fdiff;
				else
					f = ifile;

				// seek and read and decompress raw data
				uint64_t orig = io.tell(f);
				uint64_t ao = references[rr].archive_offset;
				io.seek(f, ao, SEEK_SET);
				io.read(extract_in, (32-6-8), f);
				size_t len = dup_size_compressed(extract_in);
				io.try_read(extract_in + (32-6-8), len - (32-6-8), f);
				uint64_t p;
				int r = dup_decompress(extract_in, extract_out, &len, &p);
				total_decompressed += len;

				if(r != 0 && r != 1)
					abort(true, UNITXT("Internal error, dup_decompress() = %d"), r);

				buffer_add(extract_out, references[rr].payload, references[rr].length);

				io.seek(f, orig, SEEK_SET);
				memcpy(dst + bytes_resolved, extract_out + prior, ref_has);
			}

		}
		bytes_resolved += ref_has;
	}

	return false;
}



// **************************************************************************************************************



void print_file(STRING filename, uint64_t size, tm *file_date = 0, int attributes = 0)
{	
#ifdef WINDOWS
	PRINT(6, UNITXT("%s  %c%c%c%c%c  %s  %s"), 
		size == std::numeric_limits<uint64_t>::max() ? UNITXT("                   ") : del(size, 19).c_str(),
		attributes & FILE_ATTRIBUTE_ARCHIVE ? 'A' : ' ',
		attributes & FILE_ATTRIBUTE_SYSTEM  ? 'S' : ' ',
		attributes & FILE_ATTRIBUTE_HIDDEN  ? 'H' : ' ',
		attributes & FILE_ATTRIBUTE_READONLY  ? 'R' : ' ',
		attributes & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED  ? 'I' : ' ',		
		date2str(file_date).c_str(), 		
		filename.c_str());
#else
	PRINT(6, UNITXT("%s  %s  %s"), 
		size == std::numeric_limits<uint64_t>::max() ? UNITXT("                   ") : del(size, 19).c_str(),
		date2str(file_date).c_str(), 		
		filename.c_str());
#endif
}

void save_directory(STRING base, STRING path, bool write = false)
{
	static STRING last_full = UNITXT("");
	static bool first_time = true;
	
	STRING full = base + path;
	full = remove_delimitor(full) + DELIM_STR;
	STRING full_orig = full;
	

#ifdef WINDOWS
	full = unsnap(full);

	size_t shadowsize = snappart(base + path).size();
	if(shadowsize > 0 )
	{
		if(shadowsize > base.size())
		{
			size_t remove = shadowsize - base.size();
			path = path.substr(remove);
			if(path.substr(0, 1) == DELIM_STR)
				path = path.substr(1);

			path = volpart(full_orig) + path;
		}
	}
#endif
	if(full != last_full || first_time)
	{
		contents_t c;
		c.directory = true;
		c.symlink = false;
		
		if(absolute_path)
			c.name = full;
		else
			c.name = path;

		c.link = UNITXT("");
		c.payload = 0;
		c.checksum = 0;
		contents.push_back(c);

		if(write)
		{
			io.try_write("D", 1, ofile);
			write_contents_item(ofile, &c);
		}

		PRINT(7, UNITXT("    %s%s"), STRING(full_orig != full ? UNITXT("*") : UNITXT("")).c_str(), full.c_str());
		last_full = full;
		first_time = false;
	}
}


int write_hashtable(FILE *file)
{
	size_t t = dup_table_condense();
	io.try_write("HASHTBLE", 8, file);
	io.write64(t, file);
	io.try_write(hashtable, t, file);
	io.write64(t + 8 , file);
	return 0;
}


uint64_t read_hashtable(FILE *file)
{
	uint64_t orig = seek_to_header(file, "HASHTBLE");
	uint64_t s = io.read64(file);
	io.try_read(hashtable, s, file);
	io.seek(file, orig, SEEK_SET);
	int i = dup_table_expand(s);
	abort(i != 0, UNITXT("'%s' is corrupted or not a .full file (hash table)"), slashify(full).c_str());
	return 0;
}


int write_contents(FILE *file)
{
	io.try_write("CONTENTS", 8, file);
	uint64_t w = io.write_count;
	io.write64(contents.size(), file);
	for(size_t i = 0; i < contents.size(); i++)
	{
		write_contents_item(file, &contents[i]);
	}
	io.write32(0, file);
	io.write64(io.write_count - w, file);
	return 0;
}


STRING validchars(STRING name)
{
#ifdef WINDOWS
	replace(name.begin(), name.end(), '\\', '=');
	replace(name.begin(), name.end(), '/', '=');
	replace(name.begin(), name.end(), ':', '=');
	replace(name.begin(), name.end(), '*', '=');
	replace(name.begin(), name.end(), '?', '=');
	replace(name.begin(), name.end(), '"', '=');
	replace(name.begin(), name.end(), '<', '=');
	replace(name.begin(), name.end(), '>', '=');
	replace(name.begin(), name.end(), '|', '=');
	return name;
#else
	return name;
#endif
}


void read_content_item(FILE *file, contents_t *c)
{
	c->name = slashify(io.readstr(file));
	c->link = slashify(io.readstr(file));
	c->size = io.read64(file);
	c->payload = io.read64(file);
	c->checksum = io.read64(file);
	io.read_date(&c->file_date, file);
	c->attributes = io.read32(file);
	c->directory = io.read8(file) == 0 ? false : true;
	c->symlink = io.read8(file) == 0 ? false : true;
	if(!c->directory)
	{
		STRING i = c->name;
		c->name = slashify(validchars(c->name));
		if(i != c->name)
			PRINT(2, UNITXT("*nix filename '%s' renamed to '%s'"), i.c_str(), c->name.c_str());
	}
}


uint64_t dump_contents(FILE *file)
{
	uint64_t orig = seek_to_header(file, "CONTENTS");
	uint64_t payload = 0, files = 0;

	uint64_t n = io.read64(file);
	for(uint64_t i = 0; i < n; i++)
	{
		contents_t c;
		read_content_item(file, &c);

		if(c.symlink)
		{
			print_file(STRING(c.name + UNITXT(" -> ") + STRING(c.link)).c_str(), std::numeric_limits<uint64_t>::max(), &c.file_date, c.attributes);
		}
		else if(c.directory)
		{
			if(c.name != UNITXT(".\\") && c.name != UNITXT("./") && c.name != UNITXT(""))
				save_directory(UNITXT(""), c.name);
		}
		else
		{
			payload += c.size;
			files++;
			print_file(c.name, c.size, &c.file_date, c.attributes);
		}
	}

	PRINT(8, UNITXT("%s bytes in %s file(s)"), del(payload).c_str(), del(files).c_str());

	io.seek(file, orig, SEEK_SET);
	return 0;
}


int int_flag(const string &argv, const string &flag)
{
	if(regx(argv, flag) == "")
		return -1;

	string f = regx(argv, flag + "\\d+");
	abort(f == "", UNITXT("Invalid value for -%s flag"), flag.c_str());
	int i = atoi(f.substr(1).c_str());

	return i;
}


#ifdef WINDOWS
vector<STRING> wildcard_expand(vector<STRING> files)
{   
	HANDLE hFind = INVALID_HANDLE_VALUE;
	DWORD dwError;
	WIN32_FIND_DATAW FindFileData;
	vector<STRING> ret;
	
	for(uint32_t i = 0; i < files.size(); i++)
	{
		STRING q = remove_delimitor(files[i]);
		STRING s = right(q) == UNITXT("") ? q : right(q);
		if(s.find_first_of(UNITXT("*?")) == string::npos)
		{
			ret.push_back(files[i]);
		}
		else
		{
			vector<STRING> f;
			hFind = FindFirstFileW(files[i].c_str(), &FindFileData);

			abort(!continue_flag && hFind == INVALID_HANDLE_VALUE, UNITXT("Source file(s) '%s' not found"), slashify(files[i]).c_str());

			if(hFind != INVALID_HANDLE_VALUE)
			{
				if(STRING(FindFileData.cFileName) != UNITXT(".") && STRING(FindFileData.cFileName) != UNITXT(".."))
					f.push_back(FindFileData.cFileName);
				while (FindNextFileW(hFind, &FindFileData) != 0) 
				{
					if(STRING(FindFileData.cFileName) != UNITXT(".") && STRING(FindFileData.cFileName) != UNITXT(".."))
						f.push_back(FindFileData.cFileName);
				}
    
				dwError = GetLastError();
				FindClose(hFind);
				abort(dwError != ERROR_NO_MORE_FILES, UNITXT("FindNextFile error. Error is %u"), dwError);

				size_t t = files[i].find_last_of(UNITXT("\\/"));
				STRING dir = UNITXT("");
				if(t != string::npos)
					dir = files[i].substr(0, t + 1);

				for(uint32_t j = 0; j < f.size(); j++)
					ret.push_back(dir + f[j]);
			}
		}
	}
	return ret;
}
#endif

void tidy_args(int argc2, CHR *argv2[])
{
	int i = 0;
	argc = argc2;

    for(i = 0; i < argc; i++) 
		argv.push_back(argv2[i]);

}

void parse_flags(void)
{
	int i = 1;

	while(argc > i && argv[i].substr(0, 1) == UNITXT("-") && argv[i].substr(0, 2) != UNITXT("--") && argv[i] != UNITXT("-stdin") && argv[i] != UNITXT("-stdout"))
    {
		flags = argv[i];
		i++;
        flags_exist++;


		if(flags.length () > 2 && flags.substr(0, 2) == UNITXT("-f"))
		{
			lua = flags.substr(2);
			abort(lua  == UNITXT(""), UNITXT("Missing command in -f flag"));
		}
		else if(flags.length () > 2 && flags.substr(0, 2) == UNITXT("-s"))
		{
#ifdef WINDOWS
			STRING mount = flags.substr(2);
			abort(mount == UNITXT(""), UNITXT("Missing drive in -s flag"));
			shadows.push_back(mount);
#else
			abort(true, UNITXT("-s flag not supported in *nix"));
#endif
		}
		else
		{
			size_t e = flags.find_first_not_of(UNITXT("-RroxcDupilLatgmv0123456789"));
			if(e != string::npos)
				abort(true, UNITXT("Unknown flag -%s"), flags.substr(e, 1).c_str());

			string flagsS = wstring2string(flags);

			// abort if numeric digits are used with a wrong flag
			if(regx(flagsS, "[^mgtvix0123456789][0-9]+") != "")
				abort(true, UNITXT("Numeric values must be preceded by m, g, t, v, or x"));
	

			if(regx(flagsS, "R") != "") restore_flag = true;
			if(regx(flagsS, "r") != "") recursive_flag = false;
			if(regx(flagsS, "o") != "") overwrite_flag = true;
			if(regx(flagsS, "c") != "") continue_flag = true;
			if(regx(flagsS, "D") != "") diff_flag = true;
			if(regx(flagsS, "u") != "") list_flag = true;
			if(regx(flagsS, "p") != "") named_pipes = true;
			if(regx(flagsS, "l") != "") follow_symlinks = true;
			if(regx(flagsS, "G") != "") gui_interact = true;
			if(regx(flagsS, "L") != "") list_flag  = true;
			if(regx(flagsS, "a") != "") absolute_path = true;

			//abort(overwrite_flag, UNITXT("-f flag blocked in development versions to avoid spurious writes");
	
			if(int_flag(flagsS, "t") != -1)
			{
				threads_flag = int_flag(flagsS, "t");
				if (threads_flag >= 1)
				{
					threads = threads_flag;
				}
				else
					abort(true, UNITXT("Invalid -t flag value"));
			}

			if(int_flag(flagsS, "g") != -1)
			{
				gigabyte_flag = int_flag(flagsS, "g");

				if((gigabyte_flag & (gigabyte_flag - 1)) == 0)
					memory_usage = gigabyte_flag * G;
				else
					abort(true, UNITXT("-g flag value must be a power of 2 (-g1, -g2, -g4, -g8, -g16, -g32, ...)"));
			} 

			if(int_flag(flagsS, "m") != -1)
			{
				megabyte_flag = int_flag(flagsS, "m");

				if((megabyte_flag & (megabyte_flag - 1)) == 0)
					memory_usage = megabyte_flag * M;
				else
					abort(true, UNITXT("-m flag value must be a power of 2 (-m8, -m32, -m64, -m128, -m256, ...)"));
			}

			if(int_flag(flagsS, "v") != -1)
			{
				verbose_level = int_flag(flagsS, "v");
				abort(verbose_level < 0 || verbose_level > 9, UNITXT("-v flag value must be 0...9"));
			}

			if(int_flag(flagsS, "x") != -1)
			{
				compression_level = int_flag(flagsS, "x");
				abort(compression_level > 3 || compression_level < 0, UNITXT("-x flag value must be 0...3"));
			}

			if(int_flag(flagsS, "i") != -1)
			{
				input_flag = int_flag(flagsS, "i");
				DEDUPE_LARGE = 128ULL * K * input_flag / (memory_usage * K / G);

				size_t power = 1;				
				while(power < DEDUPE_LARGE)
					power*=2;
				DEDUPE_LARGE = power;

				if(DEDUPE_LARGE > M)
					DEDUPE_LARGE = M;
				if(DEDUPE_LARGE < 32 * K)
					DEDUPE_LARGE = 32 * K;

				DEDUPE_SMALL = DEDUPE_LARGE / 32ULL;
			}

//			fprintf(stderr, "%lld, %lld\n", DEDUPE_LARGE, DEDUPE_SMALL);

		}
    } // end of while


	if (i == 1 || (!restore_flag && !list_flag))
	{
		flags = UNITXT("");
		compress_flag = true;
	}

	// todo, add s and p verification
	abort(megabyte_flag != 0 && gigabyte_flag != 0, UNITXT("-m flag not compatible with -g"));
	abort(restore_flag && (!recursive_flag || continue_flag), UNITXT("-R flag not compatible with -n or -c" ));
	abort(restore_flag && (megabyte_flag != 0 || gigabyte_flag != 0), UNITXT("-m and -t flags not applicable to restore (no memory required)"));
	abort(restore_flag && (threads_flag != 0), UNITXT("-t flag not supported for restore"));
	abort(diff_flag && compress_flag && (megabyte_flag != 0 || gigabyte_flag != 0), UNITXT("-m and -t flags not applicable to differential backup (uses same memory as full)"));
	abort(diff_flag && compress_flag && input_flag != 0, UNITXT("-i flag not applicable to differential backup"));
}


void add_item(const STRING &item)
{
	if(item.size() >= 2 && item.substr(0, 2) == UNITXT("--"))
	{
		STRING e = item.substr(2);
		e = remove_delimitor(CASESENSE(abs_path(e)));
		if(!(exists(e)))
			PRINT(4, UNITXT("Excluded item '%s' does not exist"), e.c_str());
		else
			excludelist.push_back(e);
	}
	else
		inputfiles.push_back(item);
}


void parse_files(void)
{
	// eXdupe arguments can be a little complex for the user, so we want to do exhaustive testing with good error messages2
	if(compress_flag && !diff_flag)
	{
		for(int i = flags_exist + 1; i < argc - 1; i++)
			add_item(argv.at(i));

		abort(argc - 1 < flags_exist + 2, UNITXT("Missing arguments. ") FULL_BACKUP);
		full = argv.at(argc - 1);
		if(inputfiles.at(0) == UNITXT("-stdin"))
		{
			abort(argc - 1 < flags_exist + 3, UNITXT("Missing arguments. ") FULL_BACKUP);
			name = argv.at(flags_exist + 2);
		}

		abort(inputfiles[0] == UNITXT("-stdout") || name == UNITXT("-stdin") || name == UNITXT("-stdout") || full == UNITXT("-stdin") || 
			 (inputfiles[0] == UNITXT("-stdin") && argc < 4 + flags_exist) ||
			 (inputfiles[0] != UNITXT("-stdin") && argc < 3 + flags_exist),
			 UNITXT("Syntax error in source or destination. ")
			 FULL_BACKUP);
	}
	else if(compress_flag && diff_flag)
	{
		for(int i = flags_exist + 1; i < argc - 2; i++)
			add_item(argv[i]);

		abort(argc - 1 < flags_exist + 3, UNITXT("Missing arguments. ") DIFFERENTIAL_BACKUP);
		full = argv.at(argc - 2);
		diff = argv.at(argc - 1);
		if(inputfiles[0] == UNITXT("-stdin"))
		{
			abort(argc - 1 < flags_exist + 4, UNITXT("Missing arguments. ") DIFFERENTIAL_BACKUP);
			name = argv.at(flags_exist + 2);
		}
		abort(inputfiles[0] == UNITXT("-stdin") && argc < 5 + flags_exist, UNITXT(".full file from -stdin not supported. ") DIFFERENTIAL_BACKUP);

		abort(full == UNITXT("-stdin"), UNITXT(".full file from -stdin not supported. ") DIFFERENTIAL_BACKUP);

		abort(inputfiles[0] == UNITXT("-stdout") || name == UNITXT("-stdin") || name == UNITXT("-stdout") || full == UNITXT("-stdout") || 
			 (inputfiles[0] == UNITXT("-stdin") && argc < 4 + flags_exist) ||
			 (inputfiles[0] != UNITXT("-stdin") && argc < 3 + flags_exist),
			 UNITXT("Syntax error in source or destination. ")
			 DIFFERENTIAL_BACKUP);
	}
	else if(!compress_flag && !diff_flag && !list_flag)
	{
		abort(argc - 1 < flags_exist + 2, UNITXT("Missing arguments. ") RESTORE_FULL_BACKUP);
		full = argv.at(1 + flags_exist);
		directory = argv.at(2 + flags_exist);

		abort(full == UNITXT("-stdin") && argc - 1 > flags_exist + 2, UNITXT("Too many arguments. ") RESTORE_FULL_BACKUP);

		for(int i = 0; i < argc - 3 - flags_exist; i++)
			restorelist.push_back(argv.at(i + 3 + flags_exist));

	//	abort(directory == UNITXT("-stdout"), UNITXT("Restore to stdout or non-seekable drive not supported"));
		abort(full == UNITXT("-stdout") || directory == UNITXT("-stdin") || argc < 3 + flags_exist, UNITXT("Syntax error in source or destination. ") RESTORE_FULL_BACKUP);
	}
	else if(!compress_flag && diff_flag)
	{
		abort(argc - 1 < flags_exist + 3, UNITXT("Missing arguments. ") RESTORE_DIFFERENTIAL_BACKUP);
		full = argv.at(1 + flags_exist);
		diff = argv.at(2 + flags_exist);
		directory = argv.at(3 + flags_exist);

		for(int i = 0; i < argc - 4 - flags_exist; i++)
			restorelist.push_back(argv.at(i + 4 + flags_exist));

	//	abort(directory == UNITXT("-stdout"), UNITXT("Restore to stdout or non-seekable drive not supported"));

		abort(full == UNITXT("-stdout") || diff == UNITXT("-stdout") || (full == UNITXT("-stdin") && diff == UNITXT("-stdin")) ||
		   (argc < 4 + flags_exist),
		   UNITXT("Syntax error in source or destination. ")
		   RESTORE_DIFFERENTIAL_BACKUP);
	}
	else if(list_flag)
	{
		full = argv[1 + flags_exist];
	}

	if(compress_flag && inputfiles[0] != STRING(UNITXT("-stdin")))
	{
		vector<STRING> inputfiles2;

		for(uint32_t i = 0; i < inputfiles.size(); i++)
		{
			if(abs_path(inputfiles.at(i)) == STRING(UNITXT("")))
			{
				abort(!continue_flag, UNITXT("Source file '%s' does not exist"), slashify(inputfiles[i]).c_str());
				PRINT(3, UNITXT("Skipped '%s' (file does not exist)"), slashify(inputfiles[i]).c_str());
			}
			else
			{
				inputfiles2.push_back(abs_path(inputfiles[i]));
#ifdef WINDOWS
				inputfiles2.back() = snap(inputfiles2.back());
#endif	
			}
		}
		inputfiles = inputfiles2;
#ifdef WINDOWS
		inputfiles = wildcard_expand(inputfiles);
#endif	
	}
}


void print_usage()
{
    PRINT(0, (
	//  UNITXT("================================================================================\n" // 80 chars of "="
	UNITXT("eXdupe ") + str(VERSION_MAJOR) + UNITXT(".") + str(VERSION_MINOR) + UNITXT(".") + str(VERSION_REVISION) + UNITXT(" file archiver. GPLv2 or later. Copyright 2010 - 2023.\n\n")
//	UNITXT("________________________________________________________________________________\n\n"
	FULL_BACKUP UNITXT("\n\n")
	DIFFERENTIAL_BACKUP UNITXT("\n\n")
	RESTORE_FULL_BACKUP UNITXT("\n\n")
	RESTORE_DIFFERENTIAL_BACKUP UNITXT("\n\n")
	LIST UNITXT("\n\n")
//	UNITXT("________________________________________________________________________________\n\n"
	UNITXT("Flags:\n")
	UNITXT("     -r Do not include sub directories\n")
	UNITXT("     -o Overwrite existing files (default is to abort)\n"  )
//  UNITXT("================================================================================\n
    UNITXT("     -c Continue if a source file cannot be read or does not exist (default is\n")
    UNITXT("        to abort)\n")
	UNITXT("    -xn Use compression level n for traditional data compression applied after\n")
	UNITXT("        deduplication. Set n = 0 for none (lets you apply your own), x = 1 for\n")
	UNITXT("        QuickLZ, x = 2 for zlib or x = 3 for bzip2 (default = ") + str(compression_level) + UNITXT(")\n")
	UNITXT("     -p Include named pipes\n")
	UNITXT("     -l On *nix: Follow symlinks (default is to store link only). On Windows:\n")
	UNITXT("        symlinks are always followed - storing link only is not supported\n")
//  UNITXT("================================================================================\n" // 80 chars of "="
	UNITXT("     -a Store absolute and complete paths (default is to remove the common\n")
	UNITXT("        parent path of items passed on the command line)\n")
	UNITXT(" -s\"d:\" Use Volume Shadow Copy Service for local drive d: (Windows only)\n")
	UNITXT("  -f\"s\" Filter files using a script, s, written in the Lua language\n")
	UNITXT("    -gn Use n GB memory for a hash table (default = 2). Use -mn to specify\n")
	UNITXT("        number of MB instead. Use 2 to 8 GB per TB of input data for best\n")
//  UNITXT("================================================================================\n" // 80 chars of "="
    UNITXT("        compression ratio. Differential backups will use the same memory as the\n")
    UNITXT("        full backup.\n")
    UNITXT("    -tn Use n threads (default = ") + str(threads) + UNITXT(")\n")
	UNITXT("    -vn Use verbose level n for n = 0...9 (default = ") + str(verbose_level) + UNITXT(")\n")
//	UNITXT("    -in Hint eXdupe about the input size of the backup to improve compression\n")
//	UNITXT("        ratio for diff backups. Specify n in GB (allowed to be approximate and\n")
//	UNITXT("        not very exact)\n")
//  UNITXT("================================================================================\n" // 80 chars of "="
	UNITXT("     -- Prefix items in the <sources> list with \"--\" to exclude them.\n\n")      
	//	UNITXT("________________________________________________________________________________\n\n"
	UNITXT("Quick example of backup, differential backups and a restore:\n")
#ifdef WINDOWS
    UNITXT("   eXdupe z:\\database\\ database.full\n")
	UNITXT("   eXdupe -D z:\\database\\ database.full database.diff1\n")
	UNITXT("   eXdupe -D z:\\database\\ database.full database.diff2\n")
	UNITXT("   eXdupe -RD database.full database.diff2 z:\\database\\restored\\\n\n"	)	
#else
    UNITXT("   eXdupe /database/ database.full\n")
	UNITXT("   eXdupe -D /database/ database.full database.diff1\n")
	UNITXT("   eXdupe -D /database/ database.full database.diff2\n")
	UNITXT("   eXdupe -RD database.full database.diff2 /database/restored/\n\n"		)
#endif
	UNITXT("More examples:\n")
#ifdef WINDOWS
	UNITXT("   eXdupe -stdin database.mdf -stdout < database.mdf > database.full\n")
//	UNITXT("   eXdupe -i1 z:\\smallfiles small.full \n")
	UNITXT("   eXdupe -m256t2 z:\\vmdk\\win\\ z:\\vmdk\\mac\\ z:\\vmdk\\hpux\\ servers.full\n")
	UNITXT("   eXdupe -RD servers.full servers.diff z:\\restored mac win\n")
	UNITXT("   eXdupe -c -f\"return(dir or size < 1000000)\" z:\\stuff stuff.full\n")
	UNITXT("   eXdupe -s\"c:\" c:\\ --c:\\pagefile.sys system.full\n")
#else
	UNITXT("   eXdupe -stdin database.mdf -stdout < database.mdf > database.full\n")
	UNITXT("   eXdupe -g16 /mail/ -stdout > mail.full\n")
	UNITXT("   eXdupe -m256t2 /vmdk/win/ /vmdk/mac/ /vmdk/hpux/ servers.full\n")
	UNITXT("   eXdupe -RD servers.full servers.diff /vmdk/restored mac win\n")
	UNITXT("   eXdupe /stuff -f\"return(dir or size < 1000000)\" stuff.full\n")
#endif
	UNITXT("   eXdupe -R data.full -stdout > data.xml"	)	
    ).c_str());
	exit(1);
}

FILE *try_open(const STRING &file, char mode, bool abortfail)
{
	FILE *f;
	assert(mode == 'r' || mode == 'w');
	if(file == UNITXT("-stdin"))
		f = stdin;
	else if (file == UNITXT("-stdout"))
		f = stdout;
	else
	{
		f = io.open(file.c_str(), mode);
		abort(!f && abortfail && mode == 'w', UNITXT("Error creating file '%s'"), slashify(file).c_str());
		abort(!f && abortfail && mode == 'r', UNITXT("Error opening file '%s' for reading"), slashify(file).c_str());
	}
	
	return f;
	
}

STRING parent_path(const vector<STRING> &items)
{
	size_t prefix = longest_common_prefix(items, !WIN);
	if(prefix == 0)
		return UNITXT("");


	for(uint32_t i = 0; i < items.size(); i++)
	{
		if(items[i].size() == prefix || items[i].substr(prefix - 1, 1) == STRING(DELIM_STR))
		{
			// Do nothing
		}
		else
		{
			return left(items[0].substr(prefix));
		}
	}

	return items[0].substr(0, prefix);
}

vector<STRING> leftify (const vector<STRING> &items)
{
	vector<STRING> r;
	for(size_t i = 0; i < items.size(); i++)
	{
		r.push_back(left(items[i]));
	}
	return r;
}

pair<STRING, size_t> extract_to(STRING curdir, STRING curfile)
{
	if(restorelist.size() == 0)
		return make_pair(curdir, 0);

	STRING curdir_case = CASESENSE(slashify(curdir));
	curfile = CASESENSE(curfile);

	STRING p = parent_path(restorelist);
	size_t prefix = p.size();

	for(uint32_t i = 0; i < restorelist.size(); i++)
	{
		if(curdir_case == restorelist[i])
			return make_pair(curdir.substr(prefix), i);

		if(curdir_case.substr(0, restorelist[i].size() + 1) == restorelist[i] + DELIM_STR)
			return make_pair(curdir.substr(prefix), i);

		if(curdir_case + DELIM_STR + curfile == restorelist[i])
			return make_pair(curdir.substr(left(p).size()), i);

		if(curdir_case == UNITXT("") && curfile == restorelist[i])
			return make_pair(curdir, i);

	}
	return make_pair(UNITXT(":"), 0);
}


void verify_restorelist(vector<STRING> restorelist, const vector<contents_t> &content)
{
	STRING curdir = UNITXT("");
	contents_t c;
	for(uint32_t i = 0; i < content.size(); i++)
	{
		if(restorelist.size() == 0)
			break;

		c = content[i];
		if(c.directory)
		{
			if(c.name != UNITXT("./")) // todo, simplify by not making an archive possible to contain ./ ?
				curdir = remove_delimitor(c.name);
		}
		else
		{
			pair<STRING, size_t> p = extract_to(curdir, c.name);
			STRING s = p.first;
		}
		
		pair<STRING, size_t> p = extract_to(curdir, c.name);
		size_t j = p.second;
		STRING s = p.first;
		if(s != UNITXT(":")) // : means don't extract
		{
			restorelist[j] = UNITXT(":");
		}

	}

	for(uint32_t i = 0; i < restorelist.size(); i++)
		abort(restorelist[i] != UNITXT(":"), UNITXT("'%s' does not exist in archive or is included multiple times by your [files] list"), restorelist[i].c_str());
}

void already_exists(const STRING &file)
{
	abort(file != UNITXT("-stdout") && exists(file) && !overwrite_flag, UNITXT("Destination file '%s' already exists"), slashify(file).c_str());
}

FILE *open_destination(const STRING &file)
{
	already_exists(file);
	FILE *ret;
	

#ifdef WINDOWS
	ret = try_open(file.c_str(), 'w', false);
	if(!ret) 
	{
		int attr = get_attributes(file.c_str(), follow_symlinks);
		if(attr & FILE_ATTRIBUTE_READONLY)
			set_attributes(file.c_str(), attr ^ FILE_ATTRIBUTE_READONLY);
		DeleteFile(file.c_str());
		ret = try_open(file.c_str(), 'w', true);
	}

#else
	ret = try_open(file.c_str(), 'w', true);
#endif

	return ret;
}
 
void ensure_relative(const STRING &path)
{
	STRING s = STRING(UNITXT("Archive contains absolute paths. Add a [files] argument. ")) + STRING( diff_flag ? STRING(RESTORE_DIFFERENTIAL_BACKUP) : STRING(RESTORE_FULL_BACKUP));
	abort((path.size() >= 2 && path.substr(0, 2) == UNITXT("\\\\")) || path.find_last_of(UNITXT(":")) != string::npos, s.c_str());
}

#ifndef WINDOWS
void create_symlink(STRING path, contents_t c)
{
	print_file(c.name + UNITXT(" -> ") + c.link, -1, &c.file_date, c.attributes);
	already_exists(path);
	int r = symlink(c.link.c_str(), path.c_str());
	abort(r != 0, "Error creating symlink '%s'", path.c_str());
	files++;
}
#endif

void decompress_individuals(FILE *ffull, FILE *fdiff)
{
	FILE *archive_file;
	bool pipe_out = directory == UNITXT("-stdout");

	if(diff_flag)
		archive_file = fdiff;
	else
		archive_file = ffull;

	uint64_t orig = seek_to_header(archive_file, "CONTENTS");
	uint64_t payload = 0;
	contents_t c;
	uint64_t resolved = 0;
	uint64_t basepay = 0;
	STRING curdir = UNITXT("");
	STRING root = remove_delimitor(directory);
	vector<contents_t> content;

	for(uint32_t i = 0; i < restorelist.size(); i++)
	{
		restorelist[i] = slashify(restorelist[i]);
		restorelist[i] = remove_delimitor(restorelist[i]);
		restorelist[i] = CASESENSE(restorelist[i]);
	}

	if(diff_flag)
	{
		basepay = read_references(ffull, 0);
		read_references(fdiff, basepay);
	}
	else
	{
		basepay = read_references(ffull, 0); 
	}

	uint64_t n = io.read64(archive_file);
	for(uint64_t i = 0; i < n; i++)
	{
		read_content_item(archive_file, &c);
		content.push_back(c);
	}

	verify_restorelist(restorelist, content);

	for(uint32_t i = 0; i < content.size(); i++)
	{
		c = content[i];
		if(diff_flag)
			c.payload += basepay;

		if(c.directory)
		{
			curdir = remove_delimitor(c.name);
		}

		pair<STRING, size_t> p = extract_to(curdir, c.name);
		STRING s = p.first;
		if(s != UNITXT(":")) // : means don't extract
		{
			STRING dstdir;
			STRING x = s;

			ensure_relative(x);

			if(x.substr(0, 1) != UNITXT("\\") && x.substr(0, 1) != UNITXT("/"))
				dstdir = remove_delimitor(root + DELIM_STR + x) + DELIM_STR;
			else
				dstdir = remove_delimitor(root + x) + DELIM_STR;

			if(!pipe_out)
				create_directories(dstdir);

			if(!pipe_out)
				save_directory(UNITXT(""), abs_path(dstdir));

			if(!c.directory)
			{
				checksum_t t;
				checksum_init(&t);

				if(c.symlink)
				{	
#ifdef WINDOWS
					PRINT(2, UNITXT("*nix symlink %s -> %s cannot be restored on Windows"), c.name.c_str(), c.link.c_str());
					// todo, symlink windows
#else
					update_statusbar(true, tot_res);
					create_symlink(dstdir + DELIM_STR + c.name, c);
#endif
				}
				else
				{
					print_file(c.name, c.size, &c.file_date, c.attributes);
					update_statusbar(true, tot_res);

					ofile = pipe_out ? stdout : open_destination(remove_delimitor(abs_path(dstdir)) + DELIM_STR + c.name);

					resolved = 0;

					while(resolved < c.size)
					{
						size_t process = minimum(c.size - resolved, RESTORE_CHUNKSIZE);	

						resolve(c.payload + resolved, process, extract_concatenate, ffull, fdiff, basepay);

						checksum(extract_concatenate, process, &t);
						io.write(extract_concatenate, process, ofile);
  						tot_res += process;																		          
						update_statusbar(false, tot_res);
						resolved += process;
						payload += c.size;
					}
					if(!pipe_out) 
					{
						fclose(ofile);
						set_date(dstdir + DELIM_STR + c.name, &c.file_date);
						set_attributes(dstdir + DELIM_STR + c.name, c.attributes);
					}
					abort(c.checksum != t.result, UNITXT("File checksum error"));
					files++;
				}
			}
		}
	}

	io.seek(ffull, orig, SEEK_SET);
}

void decompress_file(FILE *ofile, bool add_files)
{
	size_t a;

	STRING last_file = UNITXT("");
	uint64_t payload_orig = dup_counter_payload();

	checksum_t t;
	checksum_init(&t);
	files++;

	update_statusbar(true, dup_counter_payload()); 

	while((a = io.read(in, 8, ifile)) > 0)
	{
		size_t len, len2;
		update_statusbar(false, dup_counter_payload()); 

		if(*in == DUP_BLOCK)
		{
			uint64_t payload;

			io.try_read(in + 8, (32-6-8) - 8, ifile);
			len = dup_size_compressed(in);
			io.try_read(in + (32-6-8), len - (32-6-8), ifile);
			int r = dup_decompress(in, out, &len, &payload);
			abort(r == -1 || r == -2, UNITXT("Internal error, dup_decompress() = %p"), r);

			if(r == 0)
			{
				checksum(out, len, &t);
				io.try_write(out, len, ofile);
			}
			else if(r == 1)
			{
				if(payload >= payload_orig && add_files)
				{				
					abort(status == DIFF_RESTORE2, UNITXT("Internal error, requested seek(ofile) when status == DIFF_RESTORE2"));

					size_t fo = belongs_to(payload);
					int j = io.seek(ofile, payload - payload_orig, SEEK_SET); 

					abort(j != 0, UNITXT("Internal error 1 or non-seekable device: seek(%s, %p, %p)"), infiles[fo].filename.c_str(), payload, payload_orig);
					len2 = io.read(out, len, ofile);					
					abort(len2 != len, UNITXT("Internal error 2: read(%s, %p, %p)"), infiles[fo].filename.c_str(), len, len2);
					io.seek(ofile, 0, SEEK_END); 
					checksum(out, len, &t);

					io.try_write(out, len, ofile); 
				}
				else 
				{
					FILE *ifile2;
					size_t fo = belongs_to(payload);
					{
						ifile2 = try_open(infiles[fo].filename, 'r', true);
						infiles[fo].handle = ifile2;
						int j = io.seek(ifile2, payload - infiles[fo].offset, SEEK_SET); 
						abort(j != 0, UNITXT("Internal error 9 or destination is a non-seekable device: seek(%s, %p, %p)"), infiles[fo].filename.c_str(), payload, infiles[fo].offset);
					}
					len2 = io.read(out, len, ifile2);
					fclose(ifile2);
					abort(len2 != len, UNITXT("Internal error 10: read(%s, %p, %p)"), infiles[fo].filename.c_str(), len, len2);
					checksum((unsigned char *)out, len, &t);
					io.try_write(out, len, ofile);
				}
			}
			else
			{
				abort(true, UNITXT("Internal errror or source file corrupted: %d"), r);
			}
		}
		else
		{
			break;
		}
	}

	uint64_t c = io.read64(ifile);
	abort(c != t.result, UNITXT("File checksum error"));
}

void compress_symlink(const STRING &link, const STRING &target)
{
	(void)target;
#ifndef WINDOWS
	tm file_date;
	get_date(link, &file_date);
	memset(tmp, 0, MAX_PATH_LEN);
	int t = readlink(link.c_str(), tmp, MAX_PATH_LEN);
	if(t == -1)
	{
		PRINT(2, UNITXT("Skipped '%s' (error at readlink())"), link.c_str());
		return;
	}

	print_file(STRING(target + UNITXT(" -> ") + STRING(tmp)).c_str(), -1, &file_date);
	io.try_write("L", 1, ofile); // todo

	contents_t c;
	c.directory = false;
	c.symlink = true;
	c.link = STRING(tmp);
	c.name = target;
	c.size = 0;
	c.payload = 0;
	c.checksum = 0;
	c.file_date = file_date;
	write_contents_item(ofile, &c);

	io.try_write("ENDSENDS", 8, ofile);

	contents.push_back(c);
	files++;
	return;
#else
	PRINT(2, UNITXT("Skipped '%s' (symlinks not supported on Windows)"), link.c_str());
#endif
}


void compress_file(const STRING &input_file, const STRING &filename, const STRING &read_ahead = UNITXT(""))
{  
	if(input_file != UNITXT("-stdin") && ISNAMEDPIPE(get_attributes(input_file, follow_symlinks)) && !named_pipes)
	{
		PRINT(9, UNITXT("Skipped '%s' (no -p flag for named pipes)"), input_file.c_str());	
		return;
	}

	ifile = try_open(input_file.c_str(), 'r', false);

	tm file_date;
	int attributes = 0;
	checksum_t t;
	checksum_init(&t);
	uint64_t current_payload = 0;
	uint64_t filesize = 0;
	uint64_t file_start = dup_counter_payload();
	contents_t c;

	if(ifile)
	{
		if(input_file != UNITXT("-stdin"))
		{
			io.seek(ifile, 0, SEEK_END);
			filesize = io.tell(ifile);
			get_date(input_file, &file_date);
			attributes = get_attributes(input_file, follow_symlinks);
			print_file(filename, filesize, &file_date, attributes);
			io.seek(ifile, 0, SEEK_SET);
		}
		else
		{
			cur_date(&file_date);
			filesize = std::numeric_limits<uint64_t>::max();
		}

		size_t r;      
		update_statusbar(true, dup_counter_payload());
		io.try_write("F", 1, ofile);

		c.name = filename;
		c.size = filesize;
		c.file_date = file_date;
		c.attributes = attributes;

		write_contents_item(ofile, &c);

		files++;
		
		size_t DRC = DISK_READ_CHUNK;

		if(filesize < DISK_READ_CHUNK * threads)
			DRC = DISK_READ_CHUNK / threads;
		if(DRC < DEDUPE_LARGE)
			DRC = DEDUPE_LARGE;

		while((r = io.read(in, DRC, ifile)) > 0)
		{
			size_t c = dup_compress(in, out, r, false); // note, is allowed/possible to return 0
			add_references(out, c, io.write_count);

			checksum((unsigned char *)in, r, &t);
			io.try_write(out, c, ofile);
			update_statusbar(false, dup_counter_payload());
			current_payload += r;
		}

		// read ahead if deduplication is busy
		if(dup_busy() && read_ahead != UNITXT(""))
		{
			FILE *f = io.open(read_ahead, 'r');
			if(f != 0)
			{
				io.read(tmp, DISK_READ_CHUNK, f);
				fclose(f);
			}
		}

		r = dup_compress(0, out, 0, true);  // flush
		add_references(out, r, io.write_count);

		io.try_write(out, r, ofile);
		io.try_write("ENDSENDS", 8, ofile);
		io.write64(t.result, ofile);

		io.close(ifile);

		c.directory = false;
		c.symlink = false;
		c.name = filename;
		c.size = current_payload;
		c.payload = file_start;
		c.checksum = t.result;
		c.file_date = file_date;
		contents.push_back(c);

	}
	else if (continue_flag)
	{
		PRINT(2, UNITXT("Skipped '%s' (error opening)"), input_file.c_str());
	}
	else
	{
		abort(true, UNITXT("Error opening source file '%s'"), input_file.c_str());
	}

}


bool lua_test(STRING path, const STRING &script)
{
	if(script == UNITXT(""))
		return true;
	
#ifdef WINDOWS
	path = lcase(path);
#endif
	STRING dir = UNITXT("");
	STRING file = UNITXT("");
	uint64_t size = 0;
	STRING ext = UNITXT("");
	STRING name = UNITXT("");
	uint32_t attrib = 0;
	tm date;

#ifdef WINDOWS	
	HANDLE hFind;
	WIN32_FIND_DATAW data;
	hFind = FindFirstFileW(path.c_str() , &data); 
	attrib = data.dwFileAttributes;

	if(hFind != INVALID_HANDLE_VALUE)
		FindClose(hFind);

#endif

	get_date(path, &date);

	path = remove_delimitor(path);
	name = right(remove_delimitor(path)) == UNITXT("") ? path : right(remove_delimitor(path));

	if(is_dir(path))
	{
		dir = path;
	}
	else
	{
		size_t t = name.find_last_of(UNITXT("."));
		if(t != string::npos)
			ext = name.substr(t + 1);
		else
			ext = UNITXT("");

		file = path;
		size = filesize(path, follow_symlinks);
	}
	return execute(script, dir, file, name, size, ext, attrib, &date); 
}


bool include(const STRING &name)
{
	STRING n = remove_delimitor(CASESENSE(unsnap(abs_path(name))));

	for(uint32_t j = 0; j < excludelist.size(); j++)
	{
		STRING e = excludelist[j];
		if(n == e)
		{
			PRINT(9, UNITXT("Skipped '%s' (in '--' exclude list)"), name.c_str());
			return false;
		}
	}

	if(!lua_test(name, lua))
	{
		PRINT(9, UNITXT("Skipped '%s' (by -f filter)"), name.c_str());	
		return false;
	}
	return true;
}

void fail_list_dir(const STRING &dir)
{				
	if (continue_flag)
		PRINT(2, UNITXT("Skipped '%s' (error listing)"), dir.c_str());
	else
		abort(true, UNITXT("Error listing source directory '%s'"), dir.c_str());				

}

void compress(const STRING &base, vector<STRING> items)
{
	// Sort input items so that root items are first. Todo, can be simplified
	vector<int> attributes;

	vector<STRING> root_items;
	vector<STRING> non_root_items;
	for(uint32_t i = 0; i < items.size(); i++)
	{
		if(items[i].find(DELIM_STR) == string::npos)
			root_items.push_back(items[i]);
		else
			non_root_items.push_back(items[i]);
	}
	items.clear();
	items.insert( items.end(), root_items.begin(), root_items.end() );
	items.insert( items.end(), non_root_items.begin(), non_root_items.end() );	

	// Put item types (file, dir, symlink) in items_type[]
	// Todo, beautify by just deleting entries in 'items' instead of building an 'items2'
	vector<STRING> items2;
	for(uint32_t i = 0; i < items.size(); i++)
	{
		STRING sub = base + items[i];
		int type = get_attributes(sub, follow_symlinks);
		if(type == -1)
		{
			if(continue_flag)
			{
				PRINT(2, UNITXT("Skipped '%s' (error accessing)"), sub.c_str());
			}
			else
			{
				abort(true, UNITXT("Error accessing '%s'"), sub.c_str());
			}
		}
		else
		{
			items2.push_back(items[i]);
			attributes.push_back(type);
		}
	}
	items.clear();
	items.insert( items.end(), items2.begin(), items2.end() );

	// first process files
	for(uint32_t j = 0; j < items.size(); j++)
	{
		STRING sub = base + items[j];
		if (!ISDIR(attributes[j]) && !(ISLINK(attributes[j]) && !follow_symlinks) && include(sub))	
		{
			// we must avoid including destination file when compressing. Note that *nix is case sensitive.
			if(output_file == UNITXT("-stdout") || (CASESENSE(abs_path(sub)) != CASESENSE(abs_path(output_file))))
			{
				save_directory(base, left(items[j]) + (left(items[j]) == UNITXT("") ? UNITXT("") : DELIM_STR), true);
				STRING read_ahead = UNITXT("");
				if(j + 1 < items.size())
				{
					if(!ISDIR(attributes[j + 1]) && !ISLINK(attributes[j + 1]))
						read_ahead = base + items[j + 1];;
				}

				STRING u = items[j];
				STRING s = right(u) == UNITXT("") ? u : right(u);
				compress_file(sub, s, read_ahead);
			}
		}
	}

	// then process symlinks
	for(uint32_t j = 0; j < items.size(); j++)
	{
		STRING sub = base + items[j];
		if (ISLINK(attributes[j]) && !follow_symlinks && include(sub))
		{
			// we must avoid including destination file when compressing. Note that *nix is case sensitive.
			if(output_file == UNITXT("-stdout") || (CASESENSE(abs_path(sub)) != CASESENSE(abs_path(output_file))))
			{
				save_directory(base, left(items[j]) + (left(items[j]) == UNITXT("") ? UNITXT("") : DELIM_STR), true);
				compress_symlink(sub, right(items[j]) == UNITXT("") ? items[j] : right(items[j]));
			}
		}
	}

	// finally process directories
	for(uint32_t j = 0; j < items.size(); j++)
	{
		STRING sub = base + items[j];
		if(ISDIR(attributes[j]) && recursive_flag && include(sub))
		{
			if(items[j] != UNITXT(""))
				items[j] = remove_delimitor(items[j]) + DELIM_STR;

			vector<STRING> newdirs;
#ifdef WINDOWS
			HANDLE hFind;
			BOOL bContinue = TRUE;
			WIN32_FIND_DATAW data;
			STRING s = remove_delimitor(sub) + UNITXT("\\*");
			hFind = FindFirstFileW(s.c_str() , &data); 
			bContinue = hFind != INVALID_HANDLE_VALUE;
			
			if(hFind == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND)
				fail_list_dir(sub);
			else
			{
				while (bContinue) 
				{
					if(STRING(data.cFileName) != UNITXT(".") && STRING(data.cFileName) != UNITXT(".."))
						newdirs.push_back(items[j] + STRING(data.cFileName));
					bContinue = FindNextFileW(hFind, &data);
				}
				FindClose(hFind);
			}
#else
			struct dirent *entry;
			DIR *dir = opendir(sub.c_str());

			if(dir == 0)
				fail_list_dir(sub);
			else
			{
				while((entry = readdir(dir)) != 0)
					if(STRING(entry->d_name) != UNITXT(".") && STRING(entry->d_name) != UNITXT(".."))	
						newdirs.push_back(items[j] + entry->d_name);
			}

			closedir(dir);
#endif
			if (items[j] != UNITXT(""))
			{
				dirs++;
			}
			save_directory(base, items[j], true);
			compress(base, newdirs);
		}
	}
}


void compress_args(vector<STRING> args)
{	
	uint32_t i = 0;
	for(i = 0; i < args.size(); i++)
	{
		args[i] = remove_leading_curdir(args[i]);
		if(is_dir(args[i]))
			args[i] = remove_delimitor(args[i]) + DELIM_STR;
	}

	size_t prefix = longest_common_prefix(args, !WIN);
	base = args[0].substr(0, prefix);

	base = left(base);
	if(base != UNITXT(""))
		base += DELIM_CHAR; 
	
	for(i = 0; i < args.size(); i++)
	{
		args[i] = args[i].substr(base.length()); 
	}
	compress(base, args);
}

void decompress_sequential(const STRING &extract_dir, bool add_files)
{
    STRING curdir;
    size_t r = 0;

    curdir = extract_dir;
	//ensure_relative(curdir);
	save_directory(UNITXT(""), curdir + DELIM_STR); // initial root

    for(;;)
    {
		contents_t c;
		char w;

        r = io.read(&w, 1, ifile);
        abort(r == 0, UNITXT("Unexpected end of archive (block marker)"));

		if(w == 'D')
        {
			read_content_item(ifile, &c);
			ensure_relative(c.name);
			curdir = extract_dir + DELIM_STR + c.name;
			save_directory(UNITXT(""), curdir );
			create_directories(curdir);
        }
		else if(w == 'F')
        {
			read_content_item(ifile, &c);
			STRING buf2 = remove_delimitor(curdir) + DELIM_STR + c.name;

			print_file(c.name, c.size, &c.file_date, c.attributes);

			if(add_files)
				add_file(buf2, dup_counter_payload());

			FILE *f = open_destination(buf2);
			decompress_file(f, add_files);
			fclose(f);
        }
		else if(w == 'L')
		{
			read_content_item(ifile, &c);
#ifdef WINDOWS
#else
			STRING buf2 = curdir + DELIM_CHAR + c.name;
			create_symlink(buf2, c);
#endif
			io.read(tmp, 8, ifile); // ENDSENDS
		}

        else if(w == 'X')
		{
			return;
		}
		else
		{
           abort(true, UNITXT("Source file corrupted"));
		}
    }
}


uint32_t max_bits(uint64_t max_memory)
{
	int n = 0;
	while(dup_memory(n) <= max_memory)
		n++;
	return n - 1;
}


void write_header(FILE *file, status_t s, uint64_t mem)
{
	if(s == BACKUP)
		io.try_write("EXDUPE F", 8, file);
	else if (s == DIFF_BACKUP)
		io.try_write("EXDUPE D", 8, file);
	else
		abort(true, UNITXT("Internal error 25"));

	io.write8(VERSION_MAJOR, file);
	io.write8(VERSION_MINOR, file);
	io.write8(VERSION_REVISION, file);

	io.write64(DEDUPE_SMALL, file);
	io.write64(DEDUPE_LARGE, file);

	io.write64(mem, file);
}


uint64_t read_header(FILE *file, STRING filename, status_t expected)
{
	io.try_read(tmp, 8, file);
	if(expected == BACKUP)
		abort(!equal2(tmp, "EXDUPE F", 8), UNITXT("'%s' is corrupted or not a .full file (file header)"), filename.c_str());
	else if(expected == DIFF_BACKUP)
		abort(!equal2(tmp, "EXDUPE D", 8), UNITXT("'%s' is corrupted or not a .diff file (file header)"), filename.c_str());
	else
		abort(true, UNITXT("Internal error 278"));

	char major = io.read8(file);
	(void)major;
	char minor = io.read8(file);
	(void)minor;
	char revision = io.read8(file);
	(void)revision;

	DEDUPE_SMALL = io.read64(file);
	DEDUPE_LARGE = io.read64(file);

	abort(major != VERSION_MAJOR, UNITXT("'%s' was created with eXdupe version %d.%d.%d, please use %d.x.x on it"), filename.c_str(), major, minor, revision, major);

	return io.read64(file); // mem usage
}


void wrote_message(uint64_t bytes, uint64_t files)
{
	PRINT(5, UNITXT("WROTE %s bytes in %s file(s)"), del(bytes).c_str(), del(files).c_str()); 
}


void create_shadows(void)
{
#ifdef WINDOWS
	shadow(shadows);

	vector<pair<STRING, STRING> > snaps; //(STRING mount, STRING shadow)
	snaps = get_snaps();
	for(uint32_t i = 0; i < snaps.size(); i++)
	{
		PRINT(5, UNITXT("Created snapshot %s -> %s"), snaps[i].first.c_str(), snaps[i].second.c_str());
	}
#endif
}


#ifdef WINDOWS
int wmain(int argc2, CHR* argv2[])
#else
int main(int argc2, char* argv2[])
#endif
{
	if(argc2 == 1)
		print_usage();

	tidy_args(argc2, argv2);

	parse_flags();

	extract_in = static_cast<unsigned char*>(malloc(DEDUPE_LARGE + 1000000));
	extract_out = static_cast<unsigned char*>(malloc(DEDUPE_LARGE + 1000000));
	bits = max_bits(memory_usage);   
	tty_stderr = (_isatty(_fileno(stderr)) != 0);
	create_shadows();
	parse_files();

#ifdef WINDOWS
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

	if(restore_flag || compress_flag || list_flag) 
	{
		in = static_cast<unsigned char*>(tmalloc(DISK_READ_CHUNK));
		out = static_cast<unsigned char*>(tmalloc((threads + 1) * DISK_READ_CHUNK + M)); // todo, compute exact to save memory
	}

	if(restore_flag)
		buffer_init(RESTORE_BUFFER);

	if(list_flag)
	{
		ifile = try_open(full, 'r', true);
		dump_contents(ifile);
	}
	else if (restore_flag && full != UNITXT("-stdin") && diff != UNITXT("-stdin"))
	{
		// Restore from file.
		// =================================================================================================
		status = RESTORE;
		if(diff_flag)
		{
			FILE *fdiff = try_open(diff, 'r', true);
			FILE *ffull = try_open(full, 'r', true);
			read_header(ffull, full, BACKUP);
			read_header(fdiff, diff, DIFF_BACKUP);
			decompress_individuals(ffull, fdiff);
		}
		else
		{
			ifile = try_open(full, 'r', true);
			read_header(ifile, full, BACKUP);
			decompress_individuals(ifile, ifile);
		}
		wrote_message(tot_res, files); 
	}
	else if (((restore_flag && (full == UNITXT("-stdin"))) || diff == UNITXT("-stdin")) && restorelist.size() == 0)
    {
		// Restore from stdin. Only entire archive can be restored this way
		// =================================================================================================
		STRING s = remove_delimitor(directory);
        if(diff_flag)
		{
			status = DIFF_RESTORE1;
			ifile = try_open(full, 'r', true);
			read_header(ifile, full, BACKUP);

			create_directory((s + DELIM_CHAR + tempdiff).c_str());
			decompress_sequential(s + DELIM_CHAR + tempdiff, true);
			while(ifile == stdin && io.read(tmp, 32 * K, stdin)); // read remainder to avoid pipe err from OS
			io.close(ifile);
			status = DIFF_RESTORE2;
			PRINT(5, UNITXT("Extracted full backup into '%s'"), (s + DELIM_STR + tempdiff).c_str());		
			files = 0;
			dup_counters_reset();		
			ifile = try_open(diff, 'r', true);
			read_header(ifile, full, DIFF_BACKUP);
			decompress_sequential(s, false);
		}
		else
		{
			status = RESTORE;
			ifile = try_open(full, 'r', true);
			read_header(ifile, full, BACKUP);

			decompress_sequential(s, true);
		}

		if(diff_flag)
		{
			PRINT(5, UNITXT("Removing '%s'..."), (s + DELIM_STR + tempdiff).c_str());
			delete_directory((s + DELIM_CHAR + tempdiff).c_str());		
		}
		
		wrote_message(dup_counter_payload(), files); 

		while(ifile == stdin && io.read(tmp, 32 * K, stdin)); // read remainder to avoid pipe err from OS
        io.close(ifile);

    }

// Compress
// =================================================================================================    
    else if(compress_flag)
    {		
		if(diff_flag)
		{
			status = DIFF_BACKUP;
			ifile = try_open(full, 'r', true);
			memory_usage = read_header(ifile, full, BACKUP);
			hashtable = malloc(memory_usage);
			memset(hashtable, 0, memory_usage);
			abort(!hashtable, UNITXT("Out of memory. This differential backup requires %d MB"), dup_memory(bits) >> 20);
			int r = dup_init(DEDUPE_LARGE, DEDUPE_SMALL, memory_usage, threads, hashtable, compression_level);
			abort(r == 1, UNITXT("Out of memory. Reduce -m, -g or -t flag"));
			abort(r == 2, UNITXT("Error creating threads. Reduce -t flag"));
			read_hashtable(ifile);
			io.close(ifile);
			dup_add(false);
			output_file = diff;
		}
		else
		{
			status = BACKUP;
			hashtable = tmalloc(memory_usage);
			memset(hashtable, 0, memory_usage);
			int r = dup_init(DEDUPE_LARGE, DEDUPE_SMALL, memory_usage, threads, hashtable, compression_level);
			abort(r == 1, UNITXT("Out of memory. Reduce -m, -g or -t flag"));
			abort(r == 2, UNITXT("Error creating threads. Reduce -t flag"));
			dup_add(true);
			output_file = full;
		}
  
		ofile = open_destination(output_file);

		output_file_mine = true; // todo, can this be deleted?
		write_header(ofile, status, memory_usage);

		if(inputfiles.size() > 0 && inputfiles[0] != UNITXT("-stdin"))
		{
			compress_args(inputfiles);
		}
		else if(inputfiles.size() > 0 && inputfiles[0] == UNITXT("-stdin"))
		{
			compress_file(UNITXT("-stdin"), name);
		}

		if(files + dirs == 0) 
		{
			if(!recursive_flag)
				abort(true, UNITXT("0 source files or directories. Missing '*' wildcard with -r flag?"));
			else
				abort(true, UNITXT("0 source files or directories"));
		}
		io.try_write("X", 1, ofile);

		write_contents(ofile);

		if(!diff_flag)
		{
			write_hashtable(ofile);
			write_references(ofile);
		}
		else
		{
			write_references(ofile);
		}

		io.try_write("END", 3, ofile);

		//PRINT_MESSAGES();

	    PRINT(5, UNITXT("COMPRESSED %s bytes in %s file(s) into %s bytes"), del(dup_counter_payload()).c_str(), del(files).c_str(), del(io.write_count).c_str());
		io.close(ofile); 
	}
    else
	{
        print_usage();
	}

#ifdef WINDOWS
	unshadow();
#endif
	return 0;
}

