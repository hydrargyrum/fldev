
/* changelog :
 * 2008-04-08 removing old lib dependency, testing
 * rework May 10 2007, 23:21:44 (UTC+0200)
 *
 * prev mtime : 2007-04-03 20:52:35.000000000 +0200 */

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

#define SD strdup

#define NEW(TYPE,VAR) TYPE *VAR = malloc(sizeof (TYPE));

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
struct assoc {
	char *name;
	struct stat stat;
	PedPartition *part;
};

struct stmp {
	off_t start;
	off_t length;
	int fd;
};

/* global variables */
const char *optfile = NULL;
PedDevice *device;
PedDisk *disk;

struct stat root_stat;

struct assoc **exposed_files;

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
	return (path && path[0] == '/' && !path[1]);
}

inline struct assoc *resolve(const char *path)
{
	struct assoc **tries;
	for (tries = exposed_files; *tries != 0; tries++) {
		if (!strcmp(path, (*tries)->name)) {
			return *tries;
		}
	}
	return 0;
}

int my_parse_fargs(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	if (key == FUSE_OPT_KEY_NONOPT) {
		if (!optfile) {
			optfile = strdup(arg);
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
		struct assoc **tries = exposed_files;
		for (; *tries != 0; tries++) {
			if (!strcmp(path, (*tries)->name)) {
				memcpy(stbuf, &((*tries)->stat), sizeof(struct stat));
				return 0;
			}
		}
		return -ENOENT;
	}
}

int fs_read(const char *path, char *buffer, size_t number, off_t start, struct fuse_file_info *finfo)
{
	struct stmp *st = (struct stmp *) finfo->fh;
	
	ssize_t brd;
	size_t nnumber = number;
	off_t rstr = st->start;
	off_t length = st->length;
	int fd = st->fd;
	
	if (number + start > length) {
		number = length - start;
		printf("FIXME oldsz=%u\tnewsz=%u\tstart=%lld !\n", nnumber, number, start);
	}
	
	nnumber = number;
	lseek(fd, start + rstr, SEEK_SET);
	while (number) {
		/*rdc=pread(finfo->fh, buffer, number, (off_t) (start+ (rstr*PED_SECTOR_SIZE_DEFAULT)));*/
		brd = read(fd, buffer, number);
		if (brd < 0) {
			perror("read");
			return -errno;
		}
		if (!brd) {
			return nnumber - number;
		}
		buffer += brd;
		start += brd;
		number -= brd;
	}
	
	return nnumber;
}

int fs_write(const char *path, const char *buffer, size_t number, off_t start, struct fuse_file_info *finfo)
{
	struct stmp *st = (struct stmp *) finfo->fh;
	ssize_t bwr;
	size_t nnumber;
	off_t rstr = st->start;
	off_t length = st->length;
	int fd = st->fd;
	
	if(number + start > length) {
		number = length - start;
		printf("FIXMEw oldsz=%u\tnewsz=%u\tstart=%lld\tlen=%lld !\n", nnumber, number, start, length);
	}
	
	nnumber = number;
	lseek(fd, start + rstr, SEEK_SET);
	while (number) {
		bwr = write(fd, buffer, number);
		if (bwr < 0) {
			perror("write");
			return -errno;
		}
		/*printf("%d %d\n", bwr, number);*/
		buffer += bwr;
		start += bwr;
		number -= bwr;
	}
	
	return nnumber;
}

int fs_open(const char *path, struct fuse_file_info *finfo)
{
	PedPartition *part;
	struct assoc *file;
	
	if(finfo->flags & (O_TRUNC | O_APPEND))
		return -EPERM;
	
	file = resolve(path);
	if (!file)
		return -ENOENT;
	part = file->part;
	
	long long rstr = part->geom.start * PED_SECTOR_SIZE_DEFAULT, end = part->geom.end * PED_SECTOR_SIZE_DEFAULT;
	long long len = part->geom.length * PED_SECTOR_SIZE_DEFAULT;
	printf("open %s : start=%lld\tend=%lld\tlen=%lld\tdiff=%lld\n", path, rstr, end, len, len - (end - rstr));
	
	struct stmp *f = malloc(sizeof(struct stmp));
	P_CHECK_EXIT2(f, "malloc");
	finfo->fh = f;
	f->start = rstr;
	f->length = len;
	
	int fd = open(optfile, finfo->flags);
	f->fd = fd;
	
	if (fd < 0) {
		perror("open");
		return -errno;
	} else {
		return 0;
	}
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info)
{
	if (is_root(path)) {
		struct assoc **tmp;
		
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		
		for (tmp = exposed_files; *tmp; tmp++) {
			filler(buf, (*tmp)->name + 1, NULL, 0);
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
		struct assoc *file = resolve(path);
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
		struct assoc *file = resolve(path);
		if (!file)
			return -ENOENT;
		file->stat.st_mode = m;
	}
	return 0;
}

int fs_release(const char *path, struct fuse_file_info *finfo)
{
	struct stmp *st = (struct stmp *) finfo->fh;
	if (close(st->fd)) {
		return -errno;
	} else {
		return 0;
	}
}

void fs_destroy(void *p)
{
// 	ped_disk_destroy(disk);
// 	ped_device_close(device);
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
	printf("num : %d\n", partitions);
	ped_disk_print(disk);
	
	exposed_files = calloc(partitions, sizeof(struct assoc *));
	struct assoc **nfiles = exposed_files;
	
	root_stat.st_mode = S_IFDIR | S_IRWXU | S_IRWXO;
	root_stat.st_uid = getuid();
	root_stat.st_gid = getgid();
	
	int i;
	PedPartition *current_partition;
	char fname[10];
	
	for (i = 1; i <= partitions; i++) {
		current_partition = ped_disk_get_partition(disk, i);
		printf("%p\n", current_partition);
		if (current_partition) {
			/* check alloc */
			*nfiles = malloc(sizeof(struct assoc));
			(*nfiles)->stat.st_uid = getuid();
			(*nfiles)->stat.st_gid = getgid();
			(*nfiles)->stat.st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IROTH;
			(*nfiles)->stat.st_size = current_partition->geom.length * PED_SECTOR_SIZE_DEFAULT;
			/* TODO dir */
			
			sprintf(fname, "/hda%d", i); /* snprintf ? */
			
			(*nfiles)->name = SD(fname);
			(*nfiles)->part = current_partition;
			nfiles++;
		}
	}
	*nfiles = 0;
	
	
	/* printf("int=%d long=%d ll=%d size_t=%d ssize_t=%d off_t=%d\n", sizeof(int), sizeof(long), sizeof(long long), sizeof(size_t), sizeof(ssize_t), sizeof(off_t)); */
	
	return fuse_main(fargs.argc, fargs.argv, &fs_oper);
}



/*#define SSHFS_OPT(t, p, v) { t, offsetof(struct sshfs, p), v }*/

