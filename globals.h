#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <regex.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <unordered_set>
#include <vector>
#include <map>
#include <string>

#define VERSION      "20220107"
#define BUFFSIZE     10000
#define REGEX_MAX    10
#define META_WARN    "WARNING: Couldn't set some or all metadata."
#define ERROR_EXIT() if (flags.stop_on_error) exit(errno); else ++errors

#ifdef MAINFILE
#define EXTERN
#else
#define EXTERN extern
#endif

using namespace std;

enum
{
	VERB_NONE,
	VERB_LOW,
	VERB_HIGH
};

enum
{
	REGEX_NONE,
	REGEX_PARTIAL,
	REGEX_FULL
};

struct st_flags
{
	unsigned stop_on_error    : 1;
	unsigned delete_unmatched : 1;
	unsigned copy_dot_files   : 1;
	unsigned copy_metadata    : 1;
	unsigned compare_contents : 1;
	unsigned ignore_case      : 1;
};

EXTERN unordered_set<string> patterns;
EXTERN vector<regex_t> comp_regex;
EXTERN string dir_src;
EXTERN string dir_dest;
EXTERN struct st_flags flags;
EXTERN size_t bytes_copied;
EXTERN int verbose;
EXTERN int files_copied;
EXTERN int symlinks_copied;
EXTERN int dirs_copied;
EXTERN int unmatched_deleted;
EXTERN int regex_type;
EXTERN int errors;
EXTERN int warnings;

// copy.cc
void copyFiles(string &src_dir, string &dest_dir, int depth);

// names.cc
map<string,struct stat>::iterator findName(
	const string &name, map<string,struct stat> &names_list);
bool nameMatched(string &name);

