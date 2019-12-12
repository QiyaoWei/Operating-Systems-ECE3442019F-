#include "common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

/* make sure to use syserror() when a system call fails. see common.h */
//open, creat, read, write, close, chmod, mkdir

void
usage()
{
	fprintf(stderr, "Usage: cpr srcdir dstdir\n");
	exit(1);
}

void copyFile(char* pathname1, char* pathname2){
	int fd1, fd2;
	fd1 = open(pathname1, O_RDONLY);
	if (fd1 < 0)
		syserror(open, pathname1);

	struct stat sb;
	stat(pathname1, &sb); //used to set chmod later
	fd2 = creat(pathname2, 0777); //I believe this means all privilege
	if (fd2 < 0)
		syserror(open, pathname2);

	char buf[4096];
	ssize_t ret1 = 1;
	ssize_t ret2 = 0;
	while (ret1 > 0){
		ret1 = read(fd1, buf, 4096);
		if (ret1 < 0)
			syserror(read, pathname1);
		ret2 = write(fd2, buf, ret1); //very important! How much you write correponds to how much you read previously. Perhaps a more careful way to do this is to write with a whle loop, to be sure you have written everything
		if (ret2 < 0)
			syserror(write, pathname2);
	}

	int ret = chmod(pathname2, sb.st_mode);
	if (ret < 0)
		syserror(chmod, pathname2);

	ret1 = close(fd1);
	if (ret1 < 0)
		syserror(close, pathname1);
	ret2 = close(fd2);
	if (ret2 < 0)
		syserror(close, pathname2);
}

void copyDirectory(char* pathname1, char* pathname2){
	int ret = 0; //for mkdir

	//Investigate: strcpy accepting char* as argument???
	char p1[100];  //pathname1
	char p2[100]; //pathname2

	DIR* d = opendir(pathname1);
	struct dirent* de;

	//Investigate: what if there's no while? Just recursive
	while ((de = readdir(d)) != NULL){
		if (strcmp(de -> d_name, ".") != 0 && strcmp(de -> d_name, "..") != 0){ //don't forget to check this, otherwise won't work

			//file name parser
			strcpy(p1, pathname1);
			strcpy(p2, pathname2);
			strcat(p1, "/");
			strcat(p1, de -> d_name);
			strcat(p2, "/");
			strcat(p2, basename(p1));

			struct stat sb;
			stat(p1, &sb);
			if (S_ISREG(sb.st_mode)){ //regular file
				copyFile(p1, p2);
			}
			else if (S_ISDIR(sb.st_mode)){ //directory
				ret = mkdir(p2, 0777);
				if (ret < 0)
					syserror(mkdir, p2);
				copyDirectory(p1, p2);
				int r = chmod(p2, sb.st_mode);
				if (r < 0)
					syserror(chmod, p2);
				//Investigate: it seems like not having closedir() is not a problem
			}
			else
				printf("bad\n"); //weak error detection
		}
	}

	return;
}

int
main(int argc, char *argv[])
{

	if (argc != 3) {
		usage();
	}

	struct stat sb;
	stat(argv[1], &sb);
	int a = mkdir(argv[2], sb.st_mode); //assuming initial directory is not read-only
	if (a < 0)
		syserror(mkdir, argv[2]);

	copyDirectory(argv[1], argv[2]);

	return 0;
}
