/* This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

/* original version: 2007-04 */

/* defines */
#define _GNU_SOURCE /* get_current_dir_name */
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE
#define FUSE_USE_VERSION 25

/* includes */
#include <fuse.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <parted/parted.h>

/* my macros */
#define P_CHECK_EXIT(p) if(!p) {perror("parting");exit(127);}
#define P_CHECK_EXIT2(P,MSG) if(!P) {perror(MSG);exit(127);}

#define MIN(A,B) ((A) < (B) ? (A) : (B))

/* prototypes */
int fs_getattr(const char *path, struct stat *stbuf);
int fs_read(const char *path, char *buffer, size_t number, off_t start, struct fuse_file_info *finfo);
int fs_write(const char *path, const char *buffer, size_t number, off_t start, struct fuse_file_info *finfo);
int fs_open(const char *path, struct fuse_file_info *finfo);
int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info);
int fs_chown(const char *path, uid_t uid, gid_t gid);
int fs_chmod(const char *path, mode_t m);
int fs_release(const char *path, struct fuse_file_info *finfo);
void fs_destroy(void *p);


/* new types */
struct file {
	char *name;
	struct stat stat;
	PedPartition *partition;
};

struct dev_region {
	off_t start;
	off_t length;
	int fd;
};

/* global variables */
const char *optfile = NULL;
PedDevice *device;
PedDisk *disk;

struct stat root_stat;

struct file **exposed_files;

struct fuse_operations fs_oper = {
	.getattr = fs_getattr,
	.open = fs_open,
	.read = fs_read,
	.write = fs_write,
	.readdir = fs_readdir,
	.chown = fs_chown,
	.chmod = fs_chmod,
	.release = fs_release,
	.destroy = fs_destroy,
};

/* function definitions */
inline int is_root(const char *path)
{
	return (path && strcmp(path, "/") == 0);
}

struct file *lookup(const char *path)
{
	struct file **tries;

	for (tries = exposed_files; *tries != 0; tries++) {
		if (!strcmp(path, (*tries)->name))
			return *tries;
	}

	return 0;
}

int my_parse_fargs(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	if (key == FUSE_OPT_KEY_NONOPT) {
		if (!optfile) {
			/* take the full path, as the app could chdir() when backgrounding */
			int length = strlen(get_current_dir_name()) + strlen(arg) + 2; /* \0 and / */
			optfile = malloc(length);
			snprintf(optfile, length, "%s/%s", get_current_dir_name(), arg);
			return 0;
		}
	}
	return 1;
}

int fs_getattr(const char *path, struct stat *stbuf)
{
	if (is_root(path)) {
		memcpy(stbuf, &root_stat, sizeof(struct stat));
		return 0;
	} else {
		struct file *file = lookup(path);
		if (!file)
			return -ENOENT;
		else {
			memcpy(stbuf, &(file->stat), sizeof(struct stat));
			return 0;
		}
	}
}

int fs_read(const char *path, char *buffer, size_t number, off_t start, struct fuse_file_info *finfo)
{
	struct dev_region *region = (struct dev_region *) finfo->fh;
	size_t total_read = 0;

	number = MIN(number, region->length - start);

	lseek(region->fd, start + region->start, SEEK_SET);
	while (number) {
		/*rdc=pread(finfo->fh, buffer, number, (off_t) (start+ (rstr*PED_SECTOR_SIZE_DEFAULT)));*/
		ssize_t block_read = read(region->fd, buffer, number);
		if (block_read < 0) {
			perror("read");
			return -errno;
		}
		if (!block_read)
			break;

		buffer += block_read;
		total_read += block_read;
		number -= block_read;
	}

	return total_read;
}

int fs_write(const char *path, const char *buffer, size_t number, off_t start, struct fuse_file_info *finfo)
{
	struct dev_region *region = (struct dev_region *) finfo->fh;
	size_t total_written = 0;

	number = MIN(number, region->length - start);

	lseek(region->fd, start + region->start, SEEK_SET);
	while (number) {
		ssize_t block_written = write(region->fd, buffer, number);
		if (block_written < 0) {
			perror("write");
			return -errno;
		}

		buffer += block_written;
		total_written += block_written;
		number -= block_written;
	}

	return total_written;
}

int fs_open(const char *path, struct fuse_file_info *finfo)
{
	PedPartition *partition;
	struct file *file;
	int fd;

	if (finfo->flags & (O_TRUNC | O_APPEND))
		return -EPERM;

	file = lookup(path);
	if (!file)
		return -ENOENT;
	partition = file->partition;

	fd = open(optfile, finfo->flags);
	if (fd < 0) {
		perror("open");
		return -errno;
	}

	long long start = partition->geom.start * PED_SECTOR_SIZE_DEFAULT, end = partition->geom.end * PED_SECTOR_SIZE_DEFAULT;
	long long length = partition->geom.length * PED_SECTOR_SIZE_DEFAULT;

	struct dev_region *f = malloc(sizeof(struct dev_region));
	P_CHECK_EXIT2(f, "malloc");
	finfo->fh = f;
	f->start = start;
	f->length = length;
	f->fd = fd;

	return 0;
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info)
{
	if (is_root(path)) {
		struct file **current_file;

		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);

		for (current_file = exposed_files; *current_file; current_file++) {
			filler(buf, (*current_file)->name + 1, NULL, 0);
		}

		return 0;
	} else {
		return -ENOENT;
	}
}

int fs_chown(const char *path, uid_t uid, gid_t gid)
{
	if (is_root(path)) {
		root_stat.st_uid = uid;
		root_stat.st_gid = gid;
	} else {
		struct file *file = lookup(path);
		if (!file)
			return -ENOENT;
		file->stat.st_uid = uid;
		file->stat.st_gid = gid;
	}
	return 0;
}

int fs_chmod(const char *path, mode_t m)
{
	if (is_root(path)) {
		root_stat.st_mode = m;
	} else {
		struct file *file = lookup(path);
		if (!file)
			return -ENOENT;
		file->stat.st_mode = m;
	}
	return 0;
}

int fs_release(const char *path, struct fuse_file_info *finfo)
{
	struct dev_region *region = (struct dev_region *) finfo->fh;
	if (close(region->fd)) {
		return -errno;
	} else {
		free(region);
		return 0;
	}
}

void fs_destroy(void *p)
{
	ped_disk_destroy(disk);
	ped_device_destroy(device);
}

int main(int argc, char **argv)
{
	struct fuse_args fargs = FUSE_ARGS_INIT(argc, argv);
	fuse_opt_parse(&fargs, NULL, NULL, my_parse_fargs);

	if (!optfile) {
		printf("usage: %s <device> <mountpoint> [fuse_options]\n", argv[0]);
		exit(1);
	}

	device = ped_device_get(optfile);
	P_CHECK_EXIT2(device, "ped_device_get");
	disk = ped_disk_new(device);
	if (!disk) {
		printf("could not read partition table, check the file %s\n", optfile);
		exit(1);
	}

	int partitions = ped_disk_get_last_partition_num(disk);
	ped_disk_print(disk);

	exposed_files = calloc(partitions, sizeof(struct file *));
	struct file **nfiles = exposed_files;

	root_stat.st_mode = S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	root_stat.st_uid = getuid();
	root_stat.st_gid = getgid();

	int i;
	PedPartition *current_partition;

	for (i = 1; i <= partitions; i++) {
		current_partition = ped_disk_get_partition(disk, i);
		printf("%p\n", current_partition);
		if (current_partition) {
			char name[10];
			/* check alloc */
			*nfiles = malloc(sizeof(struct file));
			(*nfiles)->stat.st_uid = getuid();
			(*nfiles)->stat.st_gid = getgid();
			(*nfiles)->stat.st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
			(*nfiles)->stat.st_size = current_partition->geom.length * PED_SECTOR_SIZE_DEFAULT;
			/* TODO dir */

			sprintf(name, "/hda%d", i); /* snprintf ? */
			/* TODO use another pattern? depending on label-type? */

			(*nfiles)->name = strdup(name);
			(*nfiles)->partition = current_partition;
			nfiles++;
		}
	}
	*nfiles = 0;

	/* printf("int=%d long=%d ll=%d size_t=%d ssize_t=%d off_t=%d\n", sizeof(int), sizeof(long), sizeof(long long), sizeof(size_t), sizeof(ssize_t), sizeof(off_t)); */

	return fuse_main(fargs.argc, fargs.argv, &fs_oper);
}



/*#define SSHFS_OPT(t, p, v) { t, offsetof(struct sshfs, p), v }*/

