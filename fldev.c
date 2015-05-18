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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sysexits.h>

#include <fuse.h>
#include <parted/parted.h>

#define P_RETURN(P, MSG) do { int _errsave = errno; if (P) { perror(MSG); return _errsave; } } while (0)
#define UNUSED __attribute__((unused))

#define MIN(A,B) ((A) < (B) ? (A) : (B))

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

/* globals */
char *optfile = NULL;
PedDevice *device;
PedDisk *disk;
struct stat root_stat;
struct file *exposed;
int exposed_count;

/* helpers */

int is_root(const char *path)
{
	return (path && path[0] == '/' && path[1] == '\0');
}

struct file *lookup(const char *path)
{
	for (int i = 0; i < exposed_count; i++) {
		if (!strcmp(path, exposed[i].name))
			return &exposed[i];
	}
	return NULL;
}

int fldev_parse(UNUSED void *data, const char *arg, int key,
                UNUSED struct fuse_args *outargs)
{
	if (key == FUSE_OPT_KEY_NONOPT) {
		if (!optfile) {
			if (arg[0] == '/') {
				optfile = strdup(arg);
			} else {
				/* take the full path, as the app could chdir() when backgrounding */
				const char *cur = get_current_dir_name();
				if (asprintf(&optfile, "%s/%s", cur, arg) < 0) {
					return -1;
				}
			}
			return 0;
		}
	}
	return 1;
}

/* operations */

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

int fs_read(UNUSED const char *path, char *buffer, size_t count,
            off_t start, struct fuse_file_info *finfo)
{
	struct dev_region *region = (struct dev_region *) finfo->fh;
	size_t total_read = 0;

	count = MIN(count, region->length - start);

	P_RETURN(lseek(region->fd, start + region->start, SEEK_SET) < 0, "lseek");
	while (count) {
		/*rdc=pread(finfo->fh, buffer, number, (off_t) (start+ (rstr*PED_SECTOR_SIZE_DEFAULT)));*/
		ssize_t block_read = read(region->fd, buffer, count);
		P_RETURN(block_read < 0, "read");
		if (!block_read)
			break;

		buffer += block_read;
		total_read += block_read;
		count -= block_read;
	}

	return total_read;
}

int fs_write(UNUSED const char *path, const char *buffer, size_t count,
             off_t start, struct fuse_file_info *finfo)
{
	struct dev_region *region = (struct dev_region *) finfo->fh;
	size_t total_written = 0;

	count = MIN(count, region->length - start);

	P_RETURN(lseek(region->fd, start + region->start, SEEK_SET) < 0, "lseek");
	while (count) {
		ssize_t block_written = write(region->fd, buffer, count);
		P_RETURN(block_written < 0, "write");

		buffer += block_written;
		total_written += block_written;
		count -= block_written;
	}

	return total_written;
}

int fs_open(const char *path, struct fuse_file_info *finfo)
{
	if (finfo->flags & (O_TRUNC | O_APPEND))
		return -EPERM;

	struct file *file = lookup(path);
	if (!file)
		return -ENOENT;

	int fd = open(optfile, finfo->flags);
	P_RETURN(fd < 0, "open");

	PedPartition *partition = file->partition;
	uint64_t start = partition->geom.start * PED_SECTOR_SIZE_DEFAULT;
	uint64_t length = partition->geom.length * PED_SECTOR_SIZE_DEFAULT;

	struct dev_region *f = malloc(sizeof(struct dev_region));
	P_RETURN(!f, "malloc");
	finfo->fh = (uintptr_t) f;
	f->start = start;
	f->length = length;
	f->fd = fd;

	return 0;
}

int fs_release(UNUSED const char *path, struct fuse_file_info *finfo)
{
	struct dev_region *region = (struct dev_region *) finfo->fh;

	P_RETURN(close(region->fd) < 0, "close");

	free(region);
	return 0;
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               UNUSED off_t offset, UNUSED struct fuse_file_info *info)
{
	if (is_root(path)) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);

		for (int i = 0; i < exposed_count; i++) {
			filler(buf, exposed[i].name + 1, NULL, 0);
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

void fs_destroy(UNUSED void *p)
{
	ped_disk_destroy(disk);
	ped_device_destroy(device);
}

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

#define DEFAULT_DIR_MODE  S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH
#define DEFAULT_FILE_MODE  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH

int main(int argc, char **argv)
{
	struct fuse_args fargs = FUSE_ARGS_INIT(argc, argv);
	int ret;

	fuse_opt_parse(&fargs, NULL, NULL, &fldev_parse);

	if (!optfile) {
		printf("usage: %s <device> <mountpoint> [fuse_options]\n",
		       argv[0]);
		return EX_USAGE;
	}

	device = ped_device_get(optfile);
	if (!device) {
		return EX_NOINPUT;
	}
	disk = ped_disk_new(device);
	if (!disk) {
		return EX_DATAERR;
	}

	int partitions = ped_disk_get_last_partition_num(disk);
	printf("partition table:\n");
	ped_disk_print(disk);

	root_stat.st_mode = S_IFDIR | DEFAULT_DIR_MODE;
	root_stat.st_uid = getuid();
	root_stat.st_gid = getgid();

	exposed = calloc(partitions, sizeof(struct file));
	for (int i = 0; i < partitions; i++) {
		PedPartition *current_partition = ped_disk_get_partition(disk, i + 1);
		if (current_partition) {
			char *name;
			exposed[exposed_count].stat.st_uid = getuid();
			exposed[exposed_count].stat.st_gid = getgid();
			exposed[exposed_count].stat.st_mode = S_IFREG | DEFAULT_FILE_MODE;
			exposed[exposed_count].stat.st_size = current_partition->geom.length * PED_SECTOR_SIZE_DEFAULT;

			P_RETURN(asprintf(&name, "/hda%d", i + 1) < 0, "asprintf");
			/* TODO use another pattern? depending on label-type? */

			exposed[exposed_count].name = strdup(name);
			exposed[exposed_count].partition = current_partition;
			exposed_count++;
		}
	}

	ret = fuse_main(fargs.argc, fargs.argv, &fs_oper);

	/* cleanup */
	free(optfile);
	for (int i = 0; i < exposed_count; i++) {
		free(exposed[i].name);
	}
	free(exposed);
	return ret;
}
