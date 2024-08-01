#include "globals.h"

#define BUFFSIZE  10000
#define ERROR_EXIT() if (flags.stop_on_error) exit(errno); else ++errors
#define META_WARN() \
	printf("WARNING: Couldn't set metadata: %s\n",strerror(errno));
#define XATTR_WARN() \
	printf("WARNING: Couldn't set xattributes: %s\n",strerror(errno));

namespace fs = std::filesystem;

bool   loadDir(string &dirname, map<string,struct stat> &files_list);
bool   makeDir(char *src, char *dest, struct stat *src_stat, int depth);
size_t copyFile(char *src, char *dest, struct stat *src_stat);
void   copySymbolicLink(
	char *src_link,
	char *dest_link,
	struct stat *src_stat, struct stat *dest_stat, int depth);
bool   copyMetaData(char *src, char *dest, struct stat *src_stat, bool symlink);
bool   copyFileAttrs(char *dest, struct stat *src_stat);
bool   copyXAttrs(char *src, char *dest, bool symlink);
bool   sameContents(char *file1, char *file2);
char  *bytesSizeStr(size_t bytes);


/*** Copy the files from source directory to destination directory ***/
void copyFiles(string &src_dir, string &dest_dir, int depth)
{
	map<string,struct stat> src_files;
	map<string,struct stat> dest_files;
	map<string,struct stat>::iterator dest_it;
	struct stat *dest_stat;
	struct stat dest_dir_stat;
	string src_path;
	string dest_path;
	string tmp_path;
	size_t bytes;
	char *csrc_path;
	char *cdest_path;
	mode_t src_type;

	// Get info about the destination directory
	if (depth == 1)
	{
		if (lstat(dest_dir.c_str(),&dest_dir_stat) == -1)
		{
			printf("ERROR: copyFiles(): lstat(\"%s\"): %s\n",
				dest_dir.c_str(),strerror(errno));
			// Error no matter whether -e option given or not as
			// this is a critical error.
			exit(1);
		}
	}

	// Get the files to copy
	if (!loadDir(src_dir,src_files)) return;

	if (!src_files.size())
	{
		if (verbose == VERB_HIGH)
			printf("%d: No files in \"%s\"\n",depth,src_dir.c_str());
		// Don't return if set as we might find files to delete
		if (!flags.delete_unmatched) return;
	}

	// Find whats already there, doesn't matter if there's nothing
	loadDir(dest_dir,dest_files);

	if (flags.delete_unmatched)
	{
		// Delete any files in the destination dir that arn't in src.
		// To much hassle to delete directories - would need to recurse
		for(auto &[name,tmp_stat]: dest_files)
		{
			if ((tmp_stat.st_mode & S_IFMT) == S_IFREG &&
			     findName(name,src_files) == src_files.end())
			{
				tmp_path = dest_dir + "/" + name;
				if (verbose)
				{
					printf("%d: Deleting unmatched file \"%s\".\n",
						depth,tmp_path.c_str());
				}
				if (unlink(tmp_path.c_str()) == -1)
				{
					printf("ERROR: copyFiles(): unlink(\"%s\"): %s\n",
						tmp_path.c_str(),strerror(errno));
					ERROR_EXIT();
				}
				++unmatched_deleted;
			}
		}
	}

	// Go through source files and dirs to copy
	for(auto &[name,src_stat]: src_files)
	{
		src_path = src_dir + "/" + name;
		dest_path = dest_dir + "/" + name;
		csrc_path = (char *)src_path.c_str();
		cdest_path = (char *)dest_path.c_str();
		src_type = src_stat.st_mode & S_IFMT;

		// Check we're not copying a directory into itself or we'll
		// end up with recursion until we hit max path length or crash
		if (src_type == S_IFDIR &&
		    depth == 1 && 
		    src_stat.st_dev == dest_dir_stat.st_dev &&
		    src_stat.st_ino == dest_dir_stat.st_ino)
		{
			if (verbose)
			{
				printf("%d: WARNING: Cannot copy directory \"%s\" into itself.\n",
					depth,csrc_path);
			}
			++warnings;
			continue;
		}

		// Switch on the file type in the source directory
		switch(src_type)
		{
		case S_IFREG:
			// If we have patterns to match see if the file does
			if (!nameMatched(name))
			{
				if (verbose == VERB_HIGH)
				{
					printf("%d: Not copying file \"%s\" as the name doesn't match any pattern.\n",
						depth,cdest_path);
				}
				continue;
			}

			/* Find if file is in the destination directory and
			   whether its the same size. If it is then do nothing
			   unless contents differ */
			if ((dest_it = findName(name,dest_files)) != dest_files.end() &&
			     dest_it->second.st_size == src_stat.st_size)
			{
				// If flag set check contents
				if (flags.compare_contents && 
				    sameContents(csrc_path,cdest_path))
				{
					if (verbose == VERB_HIGH)
					{
						printf("%d: Not copying \"%s\" as it has the same contents as '%s'.\n",
							depth,
							cdest_path,csrc_path);
					}
					break;
				}
				if (verbose == VERB_HIGH)
				{
					printf("%d: Not copying \"%s\" as it is the same size as '%s'.\n",
						depth,cdest_path,csrc_path);
				}
				break;
			}
			if (verbose)
			{
				printf("%d: Copying file \"%s\" to \"%s\": ",
					depth,csrc_path,cdest_path);
				fflush(stdout);
			}
			bytes = copyFile(csrc_path,cdest_path,&src_stat);
			if ((long)bytes != -1 && verbose)
				printf("%s OK\n",bytesSizeStr(bytes));
			break;

		case S_IFDIR:
			if (makeDir(csrc_path,cdest_path,&src_stat,depth))
			{
				if (errno != EEXIST)
				{
					++dirs_copied;
					++total_copied;
				}
				if (verbose == VERB_HIGH)
				{
					printf("%d: Descending into directory \"%s\"...\n",
						depth,csrc_path);
				}
				copyFiles(src_path,dest_path,depth+1);
			}
			break;

		case S_IFLNK:
			if (!nameMatched(name))
			{
				if (verbose == VERB_HIGH)
				{
					printf("%d: Not copying symlink \"%s\" as the name doesn't match any pattern.\n",
						depth,cdest_path);
				}
				continue;
			}
			// See if in destination dir
			if ((dest_it = findName(name,dest_files)) != dest_files.end())
			{
				// Check if link
				dest_stat = &dest_it->second;
				if ((dest_stat->st_mode & S_IFMT) != src_type)
				{
					printf("ERROR: Destination \"%s\" exists and it is not a symlink.\n",
						cdest_path);
					ERROR_EXIT();
					break;
				}
			}
			else dest_stat = NULL;

			copySymbolicLink(
				csrc_path,cdest_path,&src_stat,dest_stat,depth);
			break;

		default:
			if (verbose == VERB_HIGH)
			{
				printf("%d: Ignoring directory entry \"%s\" of type %d\n",
					depth,csrc_path,src_type);
			}
		}
	}
	if (depth > 1)
	{
		if (verbose == VERB_HIGH)
		{
			printf("%d: Leaving directory \"%s\"...\n",
				depth,src_dir.c_str());
		}
		return;
	}

	// Back at top level , depth = 1
	if (!total_copied && !unmatched_deleted)
	{
		puts("Nothing to update.");
		return;
	}

	if (verbose) puts("\nSyncing...");
	sync();

	if (verbose)
	{
		printf("\nFiles copied        : %d (%s)\n",
			files_copied,bytesSizeStr(bytes_copied));
		printf("Symlinks copied     : %d\n",symlinks_copied);
		printf("Directories copied  : %d\n",dirs_copied);
		printf("Total FS objs copied: %d\n",total_copied);
		printf("Xattributes copied  : %d from %d filesystem objects\n",
			xattr_copied,xattr_files);
		printf("Unmatched deleted   : %d\n",unmatched_deleted);
		printf("Warnings            : %d\n",warnings);
		printf("Errors              : %d\n\n",errors);
	}
}




/*** Load the contents of a directory into files_list ***/
bool loadDir(string &dirname, map<string,struct stat> &files_list)
{
	struct stat fs;
	
	try
	{
		for(auto &file: fs::directory_iterator(dirname))
		{
			string name = file.path().filename().string();

			if (name == "." || 
			    name == ".." ||
		            (!flags.copy_dot_files && name[0] == '.')) continue;

			string path = file.path().string();
			if (lstat(path.c_str(),&fs) == -1)
			{
				printf("ERROR: loadDir(): lstat(\"%s\"): %s\n",
					path.c_str(),strerror(errno));
				ERROR_EXIT();
			}
			else files_list[name] = fs;
		}
	}
	catch(fs::filesystem_error &e)
	{
		printf("ERROR: loadDir(): directory_iterator(): %s\n",e.what());
		ERROR_EXIT();
		return false;
	}
	return true;
}




bool makeDir(char *src, char *dest, struct stat *src_stat, int depth)
{
	struct stat fs;

	if (mkdir(dest,0755) != -1)
	{
		if (verbose)
			printf("%d: Creating directory \"%s\": ",depth,dest);
		if (copyMetaData(src,dest,src_stat,false) && verbose)
			puts("OK");
		return true;
	}

	if (errno == EEXIST)
	{
		// Make sure its a dir
		if (lstat(dest,&fs) == -1)
		{
			printf("ERROR: makeDir(): lstat(\"%s\"): %s\n",
				dest,strerror(errno));
			ERROR_EXIT();
			return false;
		}
		if ((fs.st_mode & S_IFMT) != S_IFDIR)
		{
			printf("ERROR: Destination \"%s\" exists and it is not a directory.\n",dest);
			ERROR_EXIT();
			return false;
		}
	}
	else 
	{
		printf("ERROR: makeDir(): mkdir(\"%s\"): %s\n",
			dest,strerror(errno));
		ERROR_EXIT();
		return false;
	}
	return true;
}




size_t copyFile(char *src, char *dest, struct stat *src_stat)
{
	char buff[BUFFSIZE];
	size_t bytes;
	int src_fd;
	int dest_fd;
	int wrote;
	int len;

	// Open source file to read
	if ((src_fd = open(src,O_RDONLY)) == -1)
	{
		printf("ERROR: copyFile(): open(\"%s\"): %s\n",
			src,strerror(errno));
		ERROR_EXIT();
		return -1;
	}

	// Open destination file to write
	if ((dest_fd = open(
		dest,
		O_RDWR | O_CREAT | O_TRUNC,src_stat->st_mode)) == -1)
	{
		printf("ERROR: copyFile(): open(\"%s\"): %s\n",
			dest,strerror(errno));
		ERROR_EXIT();
		close(src_fd);
		return -1;
	}
	bytes = 0;
	wrote = 0;
	while((len = read(src_fd,buff,BUFFSIZE)) > 0)
	{
		if ((wrote = write(dest_fd,buff,len)) == -1)
		{
			printf("ERROR: copyFile(): write(): %s\n",strerror(errno));
			ERROR_EXIT();
			break;
		}
		bytes += wrote;
	}
	close(src_fd);
	close(dest_fd);
	if (wrote == -1) return -1;

	if (len == -1)
	{
		printf("ERROR: copyFile(): read(): %s\n",strerror(errno));
		ERROR_EXIT();
		return -1;
	}
	++files_copied;
	++total_copied;
	bytes_copied += bytes;

	return copyMetaData(src,dest,src_stat,false) ? bytes : -1;
}




void copySymbolicLink(
	char *src_link,
	char *dest_link,
	struct stat *src_stat, struct stat *dest_stat, int depth)
{
	char *src_target = new char[src_stat->st_size+1];
	char *dest_target = NULL;
	ssize_t len;

	// Auto delete mem on function exit
	unique_ptr<char[]> usrc_target(src_target);

	if ((len = readlink(src_link,src_target,src_stat->st_size)) == -1)
	{
		printf("ERROR: copySymbolicLink(): readlink(): %s\n",
			strerror(errno));
		ERROR_EXIT();
		return;
	}
	src_target[len] = 0;

	// If link already exists...
	if (dest_stat)
	{
		dest_target = new char[dest_stat->st_size+1];
		unique_ptr<char[]> udest_target(dest_target);

		if ((len = readlink(dest_link,dest_target,dest_stat->st_size)) == -1)
		{
			printf("ERROR: copySymbolicLink(): readlink(): %s\n",
				strerror(errno));
			ERROR_EXIT();
			return;
		}
		dest_target[len] = 0;

		// If its already pointing to the right thing don't do anything
		if (!strcmp(src_target,dest_target))
		{
			if (verbose == VERB_HIGH)
			{
				printf("%d: Symlink \"%s\" already exists and is set correctly.\n",
					depth,dest_link);
			}
			return;
		}

		// Pointing to something else. Only update if flag set.
		if (!flags.compare_contents) 
		{
			printf("%d: WARNING: Symlink \"%s\" already exists but -> \"%s\". \n",
				depth,dest_link,dest_target);
			return;
		}
		printf("%d: Symlink \"%s\" already exists but -> \"%s\". Deleting: ",
			depth,dest_link,dest_target);
		if (unlink(dest_link) == -1)
		{
			printf("ERROR: copySymbolicLink(): unlink(\"%s\"): %s\n",
				dest_link,strerror(errno));
			ERROR_EXIT();
			return;
		}
		puts("OK");
	}
	if (verbose)
	{
		printf("%d: Creating symlink \"%s\" -> \"%s\": ",
			depth,dest_link,src_target);
	}
	if (symlink(src_target,dest_link) == -1)
	{
		printf("ERROR: copySymbolicLink(): symlink(): %s\n",
			strerror(errno));
		ERROR_EXIT();
		return;
	}
	if (copyMetaData(src_link,dest_link,src_stat,true) && verbose)
		puts("OK");
	++symlinks_copied;
	++total_copied;
}




bool copyMetaData(char *src, char *dest, struct stat *src_stat, bool symlink)
{
	bool ret = true;

	if (flags.copy_metadata)
	{
		if (!(ret = copyFileAttrs(dest,src_stat)))
		{
			if (verbose) META_WARN();
		}
	}
	// Only try to copy xattributes if normal metadata copy went ok
	if (ret)
	{
		if (flags.copy_xattrs)
		{
			ret = copyXAttrs(src,dest,symlink);
			if (verbose && !ret) XATTR_WARN();
		}
	}
	return ret;
}




/*** Copy the standard file attributes from the source file ***/
bool copyFileAttrs(char *dest, struct stat *src_stat)
{
	if (!flags.copy_metadata) return true;

	struct timeval tv[2];
	bool ok = true;

	if (fchownat(
		AT_FDCWD,dest,
		src_stat->st_uid,src_stat->st_gid,AT_SYMLINK_NOFOLLOW) == -1)
	{
		ok = false;
	}
	/* Soft link permissions appear to be hardcoded to 777 on linux and
	   calling fchmodat() just gives an operation not supported error so
	   don't bother */
#ifdef __APPLE__
	if (fchmodat(AT_FDCWD,dest,src_stat->st_mode,AT_SYMLINK_NOFOLLOW) == -1)
		ok = false;
#endif

	// Only need time to the nearest second.
	tv[0].tv_usec = 0;
	tv[1].tv_usec = 0;
#ifdef __APPLE__
	// Apple just had to be different didn't they.
	tv[0].tv_sec = src_stat->st_atimespec.tv_sec;
	tv[1].tv_sec = src_stat->st_mtimespec.tv_sec;
#else
	tv[0].tv_sec = src_stat->st_atim.tv_sec;
	tv[1].tv_sec = src_stat->st_mtim.tv_sec;
#endif
	if (lutimes(dest,tv) == -1) ok = false; 

	warnings += (ok == false);

	return ok;
}




/*** "xattr" on MacOS command line to get/set, "attr" on linux. Linux doesn't
     allow extended attributes on soft links except under specific 
     circumstances but I've put the code in anyway because that might change
     at some point ***/
bool copyXAttrs(char *src, char *dest, bool symlink)
{
	int size;

	// Get the key list length first then allocate memory for it.
#ifdef __APPLE__
	int flags = 0;
	if (symlink) flags = XATTR_NOFOLLOW;
	if ((size = listxattr(src,NULL,0,flags)) == -1)
#else
	if (symlink)
		size = llistxattr(src,NULL,0);
	else
		size = listxattr(src,NULL,0);
	if (size == -1)
#endif
		return false;

	// Get the list of keys. Values have to be obtained seperately.
	char *keybuf = new char[size];
	unique_ptr<char[]> ukeybuf(keybuf);
#ifdef __APPLE__
	if (listxattr(src,keybuf,size,flags) == -1)
#else
	if (symlink)
		size = llistxattr(src,keybuf,size);
	else
		size = listxattr(src,keybuf,size);
	if (size == -1)
#endif
		return false;

	unique_ptr<char[]> uvalue;
	char *end = (char *)(keybuf + size);
	char *value;
	char *key;
	char *kend;
	int vallen;
	int res;

	// Go through the list of keys
	for(key=keybuf;key < end;key=kend+1)
	{
		// Should never happen but you never know
		if (!(kend = strchr(key,'\0'))) return false;

		// Get value length
#ifdef __APPLE__
		vallen = getxattr(src,key,NULL,0,0,flags);
#else
		// Linux has a seperate function for interrogating symlinks
		if (symlink)
			vallen = lgetxattr(src,key,NULL,0);
		else
			vallen = getxattr(src,key,NULL,0);
#endif
		if (vallen == -1) return false;

		value = new char[vallen+1];
		unique_ptr<char[]> uvalue(value);

		// Get the value
#ifdef __APPLE__
		res = getxattr(src,key,value,vallen,0,flags);
#else
		if (symlink)
			res = lgetxattr(src,key,value,vallen);
		else
			res = getxattr(src,key,value,vallen);
#endif
		if (res == -1) return false;
		value[vallen] = 0;
	
		// Set key-value pair in new filesystem object. For this
		// function MacOS has a positions parameter, linux doesn't.
#ifdef __APPLE__
		res = setxattr(dest,key,value,vallen,0,flags);
#else
		if (symlink)
			res = lsetxattr(dest,key,value,vallen,0);
		else
			res = setxattr(dest,key,value,vallen,0);
#endif
		if (res == -1) return false;
		++xattr_copied;
	}
	if (key != keybuf) ++xattr_files;
	return true;
}




/*** Returns true if the files have the same contents. Assumes files are the
     same size ***/
bool sameContents(char *file1, char *file2)
{
	FILE *fp1;
	FILE *fp2;
	bool ret;
	int c1;
	int c2;

	if (!(fp1 = fopen(file1,"r")))
	{
		printf("ERROR: sameContents(): fopen(\"%s\"): %s\n",
			file1,strerror(errno));
		ERROR_EXIT();
		return false;
	}
	if (!(fp2 = fopen(file2,"r")))
	{
		fclose(fp1);
		printf("ERROR: sameContents(): fopen(\"%s\"): %s\n",
			file2,strerror(errno));
		ERROR_EXIT();
		return false;
	}

	ret = true;
	do
	{
		c1 = fgetc(fp1);
		c2 = fgetc(fp2);
		if (c1 != c2)
		{
			ret = false;
			break;
		}
	} while(c1 != EOF && c2 != EOF);

	fclose(fp1);
	fclose(fp2);

	return ret;
}




char *bytesSizeStr(size_t bytes)
{
	static char str[20];

	/* Only start printing in kilobytes from 10000 as eg 2345 bytes is
	   still easy to read */
	if (bytes < 1e4)
		snprintf(str,sizeof(str),"%lu bytes",bytes);
	else if (bytes < 1e6)
		snprintf(str,sizeof(str),"%.1fK",(double)bytes / 1e3);
	else if (bytes < 1e9)
		snprintf(str,sizeof(str),"%.1fM",(double)bytes / 1e6);
	else
		snprintf(str,sizeof(str),"%.2fG",(double)bytes / 1e9);
	return str;
}
