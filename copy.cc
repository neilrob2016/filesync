#include "globals.h"

bool   loadDir(string &dirname, map<string,struct stat> &files_list);
bool   makeDir(char *dirname, struct stat *src_stat, int depth);
off_t copyFile(char *src, char *dest, struct stat *src_stat);
void   copySymbolicLink(
	char *src_link,
	char *dest_link,
	struct stat *src_stat, struct stat *dest_stat, int depth);
bool   setMetaData(char *path, struct stat *src_stat);
bool   sameContents(char *file1, char *file2);


/*** Copy the files from source directory to destination directory ***/
void copyFiles(string &src_dir, string &dest_dir, int depth)
{
	map<string,struct stat> src_files;
	map<string,struct stat> dest_files;
	map<string,struct stat>::iterator dest_it;
	struct stat *src_stat;
	struct stat *dest_stat;
	struct stat dest_dir_stat;
	string name;
	string src_path;
	string dest_path;
	off_t bytes;
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
		return;
	}

	// Find whats already there, doesn't matter if there's nothing
	loadDir(dest_dir,dest_files);

	if (flags.delete_unmatched)
	{
		// Delete any files in the destination dir that arn't in src.
		// To much hassle to delete directories - would need to recurse
		for(auto pr: dest_files)
		{
			if ((pr.second.st_mode & S_IFMT) == S_IFREG &&
			     findName(pr.first,src_files) == src_files.end())
			{
				name = dest_dir + "/" + pr.first;
				if (verbose)
				{
					printf("%d: Deleting unmatched file \"%s\".\n",
						depth,name.c_str());
				}
				if (unlink(name.c_str()) == -1)
				{
					printf("ERROR: copyFiles(): unlink(\"%s\"): %s\n",
						name.c_str(),strerror(errno));
					ERROR_EXIT();
				}
				++unmatched_deleted;
			}
		}
	}

	// Go through source files and dirs to copy
	for(auto &pr: src_files)
	{
		name = pr.first;
		src_path = src_dir + "/" + name;
		dest_path = dest_dir + "/" + name;
		csrc_path = (char *)src_path.c_str();
		cdest_path = (char *)dest_path.c_str();
		src_stat = &pr.second;
		src_type = src_stat->st_mode & S_IFMT;

		// Check we're not copying a directory into itself or we'll
		// end up with recursion until we hit max path length or crash
		if (src_type == S_IFDIR &&
		    depth == 1 && 
		    src_stat->st_dev == dest_dir_stat.st_dev &&
		    src_stat->st_ino == dest_dir_stat.st_ino)
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
			     dest_it->second.st_size == src_stat->st_size)
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
				printf("%d: Copying file \"%s\" to \"%s\"... ",
					depth,csrc_path,cdest_path);
				fflush(stdout);
			}
			bytes = copyFile(csrc_path,cdest_path,src_stat);
			if ((long)bytes != -1 && verbose)
			{
				if (bytes < 1000)
					printf("%ld bytes OK\n",(long)bytes);
				else if (bytes < 1e6)
					printf("%ldK OK\n",(long)bytes / 1000);
				else if (bytes < 1e9)
					printf("%.1fM OK\n",(double)bytes / 1e6);
				else
					printf("%.1fG OK\n",(double)bytes / 1e9);
			}
			break;

		case S_IFDIR:
			if (makeDir(cdest_path,src_stat,depth))
			{
				if (errno != EEXIST) ++dirs_copied;
				if (verbose == VERB_HIGH)
				{
					printf("%d: Descending into dir \"%s\"\n",
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
				if ((dest_it->second.st_mode & S_IFMT) != src_type)
				{
					printf("ERROR: Destination \"%s\" exists and it is not a symlink.\n",
						cdest_path);
					ERROR_EXIT();
					break;
				}
				dest_stat = &dest_it->second;
			}
			else dest_stat = NULL;

			copySymbolicLink(
				csrc_path,cdest_path,src_stat,dest_stat,depth);
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
			printf("%d: Leaving directory \"%s\"\n",
				depth,src_dir.c_str());
		}
	}
	else if (verbose)
	{
		printf("\nFiles copied      : %d\n",files_copied);
		printf("Symlinks copied   : %d\n",symlinks_copied);
		printf("Directories copied: %d\n",dirs_copied);
		printf("Unmatched deleted : %d\n",unmatched_deleted);
		printf("Warnings          : %d\n",warnings);
		printf("Errors            : %d\n\n",errors);
	}
}




/*** Load the contents of a directory into files_list ***/
bool loadDir(string &dirname, map<string,struct stat> &files_list)
{
	DIR *dir;
	struct dirent *ds;
	struct stat fs;
	string path;
	
	if (!(dir = opendir(dirname.c_str())))
	{
		printf("ERROR: loadDir(): opendir(\"%s\"): %s\n",
			dirname.c_str(),strerror(errno));
		ERROR_EXIT();
		return false;
	}
	while((ds = readdir(dir)))
	{
		if (!strcmp(ds->d_name,".") || 
		    !strcmp(ds->d_name,"..") ||
		    (!flags.copy_dot_files && ds->d_name[0] == '.')) continue;

		path = dirname + "/" + ds->d_name;
		if (lstat(path.c_str(),&fs) == -1)
		{
			printf("ERROR: loadDir(): lstat(\"%s\"): %s\n",
				path.c_str(),strerror(errno));
			ERROR_EXIT();
		}
		else files_list[ds->d_name] = fs;
	}
	closedir(dir);
	return true;
}




bool makeDir(char *dirname, struct stat *src_stat, int depth)
{
	struct stat fs;
	bool meta;

	if (mkdir(dirname,0755) != -1)
	{
		meta = setMetaData(dirname,src_stat);
		if (verbose)
		{
			printf("%d: Created directory \"%s\": ",depth,dirname);
			puts(meta ? "OK": META_WARN);
		}
		return true;
	}

	if (errno == EEXIST)
	{
		// Make sure its a dir
		if (lstat(dirname,&fs) == -1)
		{
			printf("ERROR: makeDir(): lstat(\"%s\"): %s\n",
				dirname,strerror(errno));
			ERROR_EXIT();
			return false;
		}
		if ((fs.st_mode & S_IFMT) != S_IFDIR)
		{
			printf("ERROR: Destination \"%s\" exists and it is not a directory.\n",
				dirname);
			ERROR_EXIT();
			return false;
		}
	}
	else
	{
		printf("ERROR: makeDir(): mkdir(\"%s\"): %s\n",
			dirname,strerror(errno));
		ERROR_EXIT();
		return false;
	}
	return true;
}




off_t copyFile(char *src, char *dest, struct stat *src_stat)
{
	char buff[BUFFSIZE];
	off_t bytes;
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
	if (!setMetaData(dest,src_stat) && verbose)
	{
		puts(META_WARN);
		return -1;
	}
	return bytes;
}




void copySymbolicLink(
	char *src_link,
	char *dest_link,
	struct stat *src_stat, struct stat *dest_stat, int depth)
{
	char *src_target;
	char *dest_target;
	ssize_t len;
	bool meta;

	if (!(src_target = (char *)malloc(src_stat->st_size+1)))
	{
		printf("ERROR: copySymbolicLink(): malloc(): %s\n",
			strerror(errno));
		ERROR_EXIT();
		return;
	}
	if ((len = readlink(src_link,src_target,src_stat->st_size)) == -1)
	{
		printf("ERROR: copySymbolicLink(): readlink(): %s\n",
			strerror(errno));
		ERROR_EXIT();
		free(src_target);
		return;
	}
	src_target[len] = 0;

	/* If link exists at the destination see if it points to the same
	   thing. Perhaps it would be more efficient just to recreate and
	   not check? */
	if (dest_stat)
	{
		if (!(dest_target = (char *)malloc(dest_stat->st_size+1)))
		{
			printf("ERROR: copySymbolicLink(): malloc(): %s\n",
				strerror(errno));
			ERROR_EXIT();
			free(src_target);
			return;
		}
		if ((len = readlink(dest_link,dest_target,dest_stat->st_size)) == -1)
		{
			printf("ERROR: copySymbolicLink(): readlink(): %s\n",
				strerror(errno));
			ERROR_EXIT();
			goto FREE;
		}
		dest_target[len] = 0;
		if (!strcmp(src_target,dest_target))
		{
			if (verbose == VERB_HIGH)
			{
				printf("%d: Not recreating symlink \"%s\" as it already exists as \"%s\" with the same target.\n",
					depth,src_link,dest_link);
			}
			goto FREE;
		}
		free(dest_target);
	}
	if (verbose)
	{
		printf("%d: Creating symlink \"%s\" -> \"%s\"... ",
			depth,dest_link,src_target);
	}
	unlink(dest_link);
	if (symlink(src_target,dest_link) == -1)
	{
		printf("ERROR: copySymbolicLink(): symlink(): %s\n",
			strerror(errno));
		ERROR_EXIT();
		return;
	}
	meta = setMetaData(dest_link,src_stat);
	if (verbose) puts(meta ? "OK" : META_WARN);
	free(src_target);
	++symlinks_copied;
	return;

	FREE:
	free(src_target);
	free(dest_target);
}




/*** Set the meta data in the inode ***/
bool setMetaData(char *path, struct stat *src_stat)
{
	if (!flags.copy_metadata) return true;

	struct timeval tv[2];
	bool ok = true;

	if (fchownat(
		AT_FDCWD,path,
		src_stat->st_uid,src_stat->st_gid,AT_SYMLINK_NOFOLLOW) == -1)
	{
		ok = false;
	}
#ifdef __APPLE__
	// Can get an operation not supported error with this under Linux.
	// Perhaps its just my distro/kernel?
	if (fchmodat(AT_FDCWD,path,src_stat->st_mode,AT_SYMLINK_NOFOLLOW) == -1)
#else
	if (chmod(path,src_stat->st_mode) == -1)
#endif
		ok = false;

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
	if (lutimes(path,tv) == -1) ok = false; 

	warnings += (ok == false);

	return ok;
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
