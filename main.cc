/****************************************************************************** 
 FILESYNC
 Copies files and directories from one location to another that don't exist in 
 the latter or differ (hence synchronise). Top level destination dir must exist 
 (otherwise you might as well just use cp -r).

 Original version written winter 2018-2019
******************************************************************************/
#define MAINFILE
#include "globals.h"
#include "build_date.h"

void parseCmdLine(int argc, char **argv);
void version();
void init();


int main(int argc, char **argv)
{
	parseCmdLine(argc,argv);
	if (verbose) version();
	init();
	copyFiles(dir_src,dir_dest,1);
	return 0;
}




void parseCmdLine(int argc, char **argv)
{
	int i;
	char c;

	if (argc < 2) goto USAGE;

	verbose = VERB_NORMAL;
	regex_type = REGEX_NONE;

	bzero(&flags,sizeof(flags));
	flags.stop_on_error = 1;
	flags.copy_metadata = 1;

	for(i=1;i < argc;++i)
	{
		if (argv[i][0] != '-' || strlen(argv[i]) != 2) goto USAGE;
		c = argv[i][1];

		switch(c)
		{
		case 'c':
			flags.compare_contents = 1;
			continue;
		case 'e':
			flags.stop_on_error = 0;
			continue;
		case 'h':
			goto USAGE;
		case 'i':
			flags.ignore_case = 1;
			continue;
		case 'l':
			flags.delete_unmatched = 1;
			continue;
		case 'm':
			flags.copy_metadata = 0;
			continue;
		case 'o':
			flags.copy_dot_files = 1;
			continue;
		case 'v':
			version();
			exit(0);
		case 'x':
			flags.copy_xattrs = 1;
			continue;
		}
		if (++i == argc) goto USAGE;
		switch(c)
		{
		case 'b':
			verbose = atoi(argv[i]);
			if (verbose < VERB_NONE || verbose > VERB_HIGH)
			{
				puts("ERROR: Verbosity must be 1 or 2.");
				exit(1);
			}
			break;
		case 'r':
			if (!strcasecmp(argv[i],"partial"))
				regex_type = REGEX_PARTIAL;
			else if (!strcasecmp(argv[i],"full"))
				regex_type = REGEX_FULL;
			else goto USAGE;
			break;
		case 's':
			dir_src = argv[i];
			break;
		case 'd':
			dir_dest = argv[i];
			break;
		case 'p':
			patterns.insert(argv[i]);
			break;
		default:
			goto USAGE;
		}
	}
	if (dir_src == "" || dir_dest == "")
	{
		puts("ERROR: The -s and -d arguments are required.");
		exit(1);
	}
	if (flags.ignore_case && regex_type != REGEX_NONE)
	{
		puts("ERROR: The -i and -r options are mutually exclusive.");
		exit(1);
	}
	if (!dir_dest.find(dir_src))
	{
		puts("ERROR: The destination directory is the same or a sub directory of the source directory.");
		exit(1);
	}
	return;

	USAGE:
	printf("Usage: %s\n"
	       "       -s <source dir>\n"
	       "       -d <destination dir>\n"
	       "      [-p <pattern to match>] : Wildcard by default, regex if -r option given.\n"
	       "      [-r partial/full]       : Partial or full regex matching. For partial\n"
	       "                                only some of the name needs to match the\n"
	       "                                pattern, for full the whole name must match.\n"
	       "      [-b <verbosity level>]  : %d to %d. Default = %d.\n"
	       "      [-c]                    : Compare file contents, not just size. This\n"
	       "                                might be very slow for large files.\n"
	       "      [-e]                    : Do NOT stop on errors.\n"
	       "      [-h]                    : Show this usage.\n"
	       "      [-i]                    : Ignore case in names when not using regex.\n"
	       "                                Meant for OSX which has a case insensitive\n"
	       "                                file system by default.\n"
	       "      [-l]                    : Delete files (not dirs) in destination that\n"
	       "                                don't exist in the source.\n"
	       "      [-m]                    : Do NOT copy standard file metadata. ie: mode,\n"
	       "                                user & group id, access and modification times.\n"
	       "      [-o]                    : Copy (and delete if -l) dot files and\n"
	       "                                directories. eg: .profile\n"
	       "      [-v]                    : Print version and exit.\n"
	       "      [-x]                    : Copy extended attributes if possible. If it\n"
	       "                                fails a warning is given, not a fatal error.\n"
	       "Note: The -p argument restricts files and symlinks copied to those that match\n"
	       "      the given pattern(s). The option must be used once for each pattern.\n"
	       "      The patterns use '*' and '?' to match unless the regular expression -r\n"
	       "      option is given.\n",
		argv[0],VERB_NONE,VERB_HIGH,VERB_NORMAL);
	exit(1);
}




void version()
{
	puts("\n*** FILESYNC ***\n");
	puts("Copyright (C) Neil Robertson 2021-2024\n");
	printf("Version   : %s\n",VERSION);
	printf("Build date: %s\n\n",BUILD_DATE);
}




void init()
{
	regex_t regex;
	char errstr[100];
	int err;

	bytes_copied = 0;
	files_copied = 0;
	symlinks_copied = 0;
	dirs_copied = 0;
	unmatched_deleted = 0;
	errors = 0;
	warnings = 0;

	if (regex_type != REGEX_NONE)
	{
		for(auto pat: patterns)
		{
			if ((err = regcomp(
				&regex,pat.c_str(),REG_EXTENDED)))
			{
				regerror(err,&regex,errstr,sizeof(errstr));
				printf("ERROR: Invalid regex: %s\n",errstr);
				exit(1);
			}
			comp_regex.push_back(regex);
		}
	}
}
