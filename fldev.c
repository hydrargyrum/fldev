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
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sysexits.h>
#include <unistd.h>

#include <fuse.h>
#include <parted/parted.h>

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
struct g {
	char *optfile;
	char *prefix;
	int prefix_allocated;
	PedDevice *device;
	PedDisk *disk;
	struct stat root_stat;
	struct file *exposed;
	int exposed_count;
} G;

/* helpers */

__attribute__ ((warn_unused_result))
int perror_errno(const char *msg)
{
	int saved = errno;
	perror(msg);
	return -saved;
}

int is_root(const char *path)
{
	return (path && path[0] == '/' && path[1] == '\0');
}

struct file *lookup(const char *path)
{
	for (int i = 0; i < G.exposed_count; i++) {
		struct file *file = &G.exposed[i];
		if (!strcmp(path, file->name))
			return file;
	}
	return NULL;
}

int fldev_parse(UNUSED void *data, const char *arg, int key,
                UNUSED struct fuse_args *outargs)
{
	if (key == FUSE_OPT_KEY_NONOPT) {
		if (!G.optfile) {
			if (arg[0] == '/') {
				G.optfile = strdup(arg);
			} else {
				/* take the full path, as the app could chdir() when backgrounding */
				char *cur = get_current_dir_name();
				if (asprintf(&G.optfile, "%s/%s", cur, arg) < 0) {
					perror("asprintf");
					free(cur);
					return -1;
				}
				free(cur);
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
		*stbuf = G.root_stat;
		return 0;
	} else {
		struct file *file = lookup(path);
		if (!file)
			return -ENOENT;
		else {
			*stbuf = file->stat;
			return 0;
		}
	}
}

int fs_read(UNUSED const char *path, char *buffer, size_t count,
            off_t start, struct fuse_file_info *finfo)
{
	struct dev_region *region = (struct dev_region *)(uintptr_t)finfo->fh;
	size_t total_read = 0;

	count = MIN(count, region->length - start);

	if (lseek(region->fd, start + region->start, SEEK_SET) < 0)
		return perror_errno("lseek");

	while (count) {
		/*rdc=pread(finfo->fh, buffer, number, (off_t) (start+ (rstr*PED_SECTOR_SIZE_DEFAULT)));*/
		ssize_t block_read = read(region->fd, buffer, count);
		if (block_read < 0)
			return perror_errno("read");
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
	struct dev_region *region = (struct dev_region *)(uintptr_t)finfo->fh;
	size_t total_written = 0;

	count = MIN(count, region->length - start);

	if (lseek(region->fd, start + region->start, SEEK_SET) < 0)
		return perror_errno("lseek");
	while (count) {
		ssize_t block_written = write(region->fd, buffer, count);
		if (block_written < 0)
			return perror_errno("write");

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

	int fd = open(G.optfile, finfo->flags);
	if (fd < 0)
		return perror_errno("open");

	PedPartition *partition = file->partition;
	uint64_t start = partition->geom.start * PED_SECTOR_SIZE_DEFAULT;
	uint64_t length = partition->geom.length * PED_SECTOR_SIZE_DEFAULT;

	struct dev_region *f = malloc(sizeof(struct dev_region));
	if (!f)
		return perror_errno("malloc");
	finfo->fh = (uintptr_t) f;
	f->start = start;
	f->length = length;
	f->fd = fd;

	return 0;
}

int fs_release(UNUSED const char *path, struct fuse_file_info *finfo)
{
	struct dev_region *region = (struct dev_region *)(uintptr_t)finfo->fh;

	if (close(region->fd) < 0)
		return perror_errno("close");

	free(region);
	return 0;
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               UNUSED off_t offset, UNUSED struct fuse_file_info *info)
{
	if (is_root(path)) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);

		for (int i = 0; i < G.exposed_count; i++) {
			filler(buf, G.exposed[i].name + 1, NULL, 0);
		}

		return 0;
	} else {
		return -ENOENT;
	}
}

int fs_chown(const char *path, uid_t uid, gid_t gid)
{
	if (is_root(path)) {
		G.root_stat.st_uid = uid;
		G.root_stat.st_gid = gid;
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
		G.root_stat.st_mode = m;
	} else {
		struct file *file = lookup(path);
		if (!file)
			return -ENOENT;
		file->stat.st_mode = m;
	}
	return 0;
}

struct fuse_operations fs_oper = {
	.getattr = fs_getattr,
	.open = fs_open,
	.release = fs_release,
	.read = fs_read,
	.write = fs_write,
	.readdir = fs_readdir,
	.chown = fs_chown,
	.chmod = fs_chmod,
};

#define DEFAULT_DIR_MODE  S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH
#define DEFAULT_FILE_MODE  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH

int main(int argc, char **argv)
{
	struct fuse_args fargs = FUSE_ARGS_INIT(argc, argv);
	struct fuse_opt opts[] = {
		{
			.templ = "prefix=%s",
			.offset = offsetof(struct g, prefix),
		},
		{
			.templ = "prefix=",
			.offset = offsetof(struct g, prefix_allocated),
			.value = 1,
		},
		FUSE_OPT_END
	};
	int ret;

	fuse_opt_parse(&fargs, &G, opts, &fldev_parse);

	if (!G.optfile) {
		printf("usage: %s <device> <mountpoint> [fuse_options]\n",
		       argv[0]);
		return EX_USAGE;
	}
	if (!G.prefix) {
		G.prefix = "hda";
	}

	G.device = ped_device_get(G.optfile);
	if (!G.device) {
		return EX_NOINPUT;
	}
	G.disk = ped_disk_new(G.device);
	if (!G.disk) {
		return EX_DATAERR;
	}

	int partitions = ped_disk_get_last_partition_num(G.disk);
	printf("partition table:\n");
	ped_disk_print(G.disk);

	G.root_stat.st_mode = S_IFDIR | DEFAULT_DIR_MODE;
	G.root_stat.st_uid = getuid();
	G.root_stat.st_gid = getgid();

	G.exposed = calloc(partitions, sizeof(struct file));
	for (int i = 0; i < partitions; i++) {
		PedPartition *current_partition = ped_disk_get_partition(G.disk, i + 1);
		struct file *file = &G.exposed[G.exposed_count];

		if (current_partition) {
			file->stat.st_uid = getuid();
			file->stat.st_gid = getgid();
			file->stat.st_mode = S_IFREG | DEFAULT_FILE_MODE;
			file->stat.st_size = current_partition->geom.length * PED_SECTOR_SIZE_DEFAULT;

			if (asprintf(&file->name, "/%s%d", G.prefix, i + 1) < 0) {
				perror("asprintf");
				ret = EX_OSERR;
				goto cleanup;
			}
			/* TODO use another pattern? depending on label-type? */

			file->partition = current_partition;
			G.exposed_count++;
		}
	}

	ret = fuse_main(fargs.argc, fargs.argv, &fs_oper);

	/* cleanup */
cleanup:
	ped_disk_destroy(G.disk);
	ped_device_destroy(G.device);

	free(G.optfile);
	if (G.prefix_allocated)
		free(G.prefix);
	for (int i = 0; i < G.exposed_count; i++) {
		free(G.exposed[i].name);
	}
	free(G.exposed);
	return ret;
}
