#include "globals.h"

bool wildMatch(const char *str, const char *pat);


/*** Find the filesystem object name in the map ***/
map<string,struct stat>::iterator findName(
	const string &name, map<string,struct stat> &names_list)
{
	map<string,struct stat>::iterator mit;

	if (!flags.ignore_case) return names_list.find(name);

	for(mit=names_list.begin();
	    mit != names_list.end() && 
	    strcasecmp(mit->first.c_str(),name.c_str());++mit);

	return mit;
}




/*** See if the file matched any patterns given with -p ***/
bool nameMatched(string &name)
{
	regmatch_t pmatch[REGEX_MAX];
	int i;

	// If no patterns then always match
	if (!patterns.size()) return true;

	if (regex_type == REGEX_NONE)
	{
		// Wildcard matching
		for(auto pat: patterns)
			if (wildMatch(name.c_str(),pat.c_str())) return true;
		return false;
	}

	// Regex matching
	for(regex_t regex: comp_regex)
	{
		if (regexec(&regex,name.c_str(),REGEX_MAX,pmatch,0) == REG_NOMATCH)
			continue;

		// If partial then any match will do
		if (regex_type == REGEX_PARTIAL) return true;

		// Go through all the matches and look for full match
		for(i=0;pmatch[i].rm_so != -1;++i)
		{
			if (!pmatch[i].rm_so && 
			    pmatch[i].rm_eo == (int)name.length()) return true;
		}
		return false;
	}
	return false;
}




/*** Returns true if the string matches the pattern, else false. Supports 
     wildcard patterns containing '*' and '?' ***/
bool wildMatch(const char *str, const char *pat)
{
	const char *s,*p,*s2;

	for(s=str,p=pat;*s && *p;++s,++p)
	{
		switch(*p)
		{
		case '?':
			continue;

		case '*':
			if (!*(p+1)) return true;

			for(s2=s;*s2;++s2)
				if (wildMatch(s2,p+1)) return true;
			return false;
		}
		if (*s != *p) return false;
	}

	return (!*s && !*p);
}
