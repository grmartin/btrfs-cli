// SPDX-License-Identifier: GPL-2.0-only
/*
 * btrfs-cli: list and extract files from a btrfs image without mounting it.
 *
 * Built on the read-only btrfs implementation from btrfs-fuse
 * (https://github.com/adam900710/btrfs-fuse), minus the FUSE layer.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ctree.h"
#include "inode.h"
#include "data.h"
#include "super.h"
#include "volumes.h"

#define READ_CHUNK	(1024 * 1024)
#define MAX_EXTRA_DEVS	32

static const char *prog = "btrfs-cli";

static void usage(FILE *out)
{
	fprintf(out,
"usage: %s list [-l] [-d <device>]... <image> [<path>]\n"
"       %s get  [-o <output>] [-d <device>]... <image> <path>\n"
"\n"
"commands:\n"
"  list   recursively list every file under <path> (default: /)\n"
"  get    write the contents of the file at <path> to stdout, or to <output>\n"
"\n"
"options:\n"
"  -l           long listing: type/mode, size, mtime, and symlink targets\n"
"  -o <output>  write file data to <output> instead of stdout\n"
"  -d <device>  extra device image of a multi-device filesystem (repeatable)\n"
"  -h           show this help\n"
"\n"
"Paths are absolute within the image's default subvolume, e.g. /etc/hostname.\n",
		prog, prog);
}

/*
 * Turn a user-supplied path into the form btrfs_resolve_path() expects:
 * absolute, with "." and ".." already resolved. Returns the length, or -1 if
 * it doesn't fit in @out.
 */
static int normalize_path(const char *in, char *out, size_t out_size)
{
	size_t len = 0;
	const char *p = in;

	if (out_size < 2)
		return -1;
	out[0] = '\0';

	while (*p) {
		const char *end;
		size_t n;

		while (*p == '/')
			p++;
		if (!*p)
			break;
		end = strchrnul(p, '/');
		n = end - p;

		if (n == 1 && p[0] == '.') {
			/* nothing */
		} else if (n == 2 && p[0] == '.' && p[1] == '.') {
			while (len > 0 && out[len - 1] != '/')
				len--;
			if (len > 0)
				len--;
		} else {
			if (len + 1 + n + 1 > out_size)
				return -1;
			out[len++] = '/';
			memcpy(out + len, p, n);
			len += n;
		}
		p = end;
	}
	if (len == 0)
		out[len++] = '/';
	out[len] = '\0';
	return (int)len;
}

static char type_char(mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFDIR:	return 'd';
	case S_IFLNK:	return 'l';
	case S_IFCHR:	return 'c';
	case S_IFBLK:	return 'b';
	case S_IFIFO:	return 'p';
	case S_IFSOCK:	return 's';
	default:	return '-';
	}
}

static void format_mode(mode_t mode, char buf[11])
{
	static const char rwx[] = "rwxrwxrwx";
	int i;

	buf[0] = type_char(mode);
	for (i = 0; i < 9; i++)
		buf[i + 1] = (mode & (1 << (8 - i))) ? rwx[i] : '-';
	if (mode & S_ISUID)
		buf[3] = (mode & S_IXUSR) ? 's' : 'S';
	if (mode & S_ISGID)
		buf[6] = (mode & S_IXGRP) ? 's' : 'S';
	if (mode & S_ISVTX)
		buf[9] = (mode & S_IXOTH) ? 't' : 'T';
	buf[10] = '\0';
}

static void print_entry(struct btrfs_fs_info *fs_info,
			struct btrfs_inode *inode, const char *path,
			bool long_fmt)
{
	char mode_str[11];
	char time_str[32];
	char target[PATH_MAX];
	struct stat st = {};
	struct tm tm;
	int ret;

	if (!long_fmt) {
		puts(path);
		return;
	}

	ret = btrfs_stat(fs_info, inode, &st);
	if (ret < 0) {
		fprintf(stderr, "%s: stat %s: %s\n", prog, path, strerror(-ret));
		printf("?????????? %12s %16s %s\n", "?", "?", path);
		return;
	}

	format_mode(st.st_mode, mode_str);
	if (localtime_r(&st.st_mtim.tv_sec, &tm))
		strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", &tm);
	else
		snprintf(time_str, sizeof(time_str), "?");
	printf("%s %12lld %16s %s", mode_str, (long long)st.st_size, time_str,
	       path);

	if (inode->file_type == BTRFS_FT_SYMLINK) {
		ret = btrfs_read_link(fs_info, inode, target, sizeof(target));
		if (ret > 0)
			printf(" -> %s", target);
	}
	putchar('\n');
}

/*
 * Print every entry below @dir. @path holds @dir's path (no trailing '/'
 * except for the root) and is used as scratch space for children's paths.
 */
static int list_dir(struct btrfs_fs_info *fs_info, struct btrfs_inode *dir,
		    char *path, size_t path_len, bool long_fmt)
{
	struct btrfs_iterate_dir_ctrl ctrl = {};
	size_t base_len = (path_len == 1) ? 0 : path_len;
	int err = 0;
	int ret;

	ret = btrfs_iterate_dir_start(fs_info, &ctrl, dir, 0);
	while (ret == 0) {
		char name[BTRFS_NAME_LEN + 1] = {};
		struct btrfs_inode entry = {};
		size_t name_len;
		u64 index;

		ret = btrfs_iterate_dir_get_inode(fs_info, &ctrl, &entry,
						  &index, name, &name_len);
		if (ret < 0)
			break;

		if (base_len + 1 + name_len + 1 > PATH_MAX) {
			fprintf(stderr, "%s: path too long under %.*s\n", prog,
				(int)path_len, path);
			err = -ENAMETOOLONG;
		} else {
			path[base_len] = '/';
			memcpy(path + base_len + 1, name, name_len);
			path[base_len + 1 + name_len] = '\0';

			print_entry(fs_info, &entry, path, long_fmt);
			if (entry.file_type == BTRFS_FT_DIR) {
				int sub = list_dir(fs_info, &entry, path,
						   base_len + 1 + name_len,
						   long_fmt);
				if (sub < 0)
					err = sub;
			}
			path[path_len] = '\0';
		}
		ret = btrfs_iterate_dir_next(fs_info, &ctrl);
	}
	btrfs_iterate_dir_end(fs_info, &ctrl);

	if (ret < 0) {
		fprintf(stderr, "%s: reading directory %s: %s\n", prog, path,
			strerror(-ret));
		return ret;
	}
	return err;
}

static int cmd_list(struct btrfs_fs_info *fs_info, const char *user_path,
		    bool long_fmt)
{
	struct btrfs_inode inode = {};
	char path[PATH_MAX];
	int len;
	int ret;

	len = normalize_path(user_path, path, sizeof(path));
	if (len < 0) {
		fprintf(stderr, "%s: path too long: %s\n", prog, user_path);
		return -ENAMETOOLONG;
	}

	ret = btrfs_resolve_path(fs_info, path, len, &inode);
	if (ret < 0) {
		fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(-ret));
		return ret;
	}

	if (inode.file_type != BTRFS_FT_DIR) {
		print_entry(fs_info, &inode, path, long_fmt);
		return 0;
	}
	return list_dir(fs_info, &inode, path, len, long_fmt);
}

static int write_all(int fd, const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t n = write(fd, buf, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		buf += n;
		len -= n;
	}
	return 0;
}

static int cmd_get(struct btrfs_fs_info *fs_info, const char *user_path,
		   const char *output)
{
	struct btrfs_inode inode = {};
	struct stat st = {};
	char path[PATH_MAX];
	char *buf = NULL;
	u64 offset = 0;
	int out_fd = STDOUT_FILENO;
	int len;
	int ret;

	len = normalize_path(user_path, path, sizeof(path));
	if (len < 0) {
		fprintf(stderr, "%s: path too long: %s\n", prog, user_path);
		return -ENAMETOOLONG;
	}

	ret = btrfs_resolve_path(fs_info, path, len, &inode);
	if (ret < 0) {
		fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(-ret));
		return ret;
	}
	if (inode.file_type == BTRFS_FT_DIR) {
		fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(EISDIR));
		return -EISDIR;
	}
	if (inode.file_type != BTRFS_FT_REG_FILE) {
		fprintf(stderr, "%s: %s: not a regular file\n", prog, path);
		return -EINVAL;
	}

	ret = btrfs_stat(fs_info, &inode, &st);
	if (ret < 0) {
		fprintf(stderr, "%s: stat %s: %s\n", prog, path, strerror(-ret));
		return ret;
	}

	/* READ_CHUNK is a multiple of every valid btrfs sectorsize. */
	buf = malloc(READ_CHUNK);
	if (!buf)
		return -ENOMEM;

	if (output) {
		out_fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (out_fd < 0) {
			ret = -errno;
			fprintf(stderr, "%s: %s: %s\n", prog, output,
				strerror(errno));
			goto out;
		}
	}

	/* btrfs_read_file() needs sector-aligned offsets and lengths. */
	while (offset < (u64)st.st_size) {
		u64 want = (u64)st.st_size - offset;
		ssize_t got;

		if (want > READ_CHUNK)
			want = READ_CHUNK;
		got = btrfs_read_file(fs_info, &inode, offset, buf,
				      round_up(want, fs_info->sectorsize));
		if (got < 0) {
			ret = got;
			fprintf(stderr, "%s: reading %s at offset %llu: %s\n",
				prog, path, (unsigned long long)offset,
				strerror(-ret));
			goto out;
		}
		if (got == 0) {
			ret = -EIO;
			fprintf(stderr, "%s: short read of %s at offset %llu\n",
				prog, path, (unsigned long long)offset);
			goto out;
		}
		if ((u64)got > want)
			got = want;

		ret = write_all(out_fd, buf, got);
		if (ret < 0) {
			fprintf(stderr, "%s: write: %s\n", prog, strerror(-ret));
			goto out;
		}
		offset += got;
	}
	ret = 0;
out:
	if (output && out_fd >= 0 && close(out_fd) < 0 && ret == 0) {
		ret = -errno;
		fprintf(stderr, "%s: %s: %s\n", prog, output, strerror(errno));
	}
	free(buf);
	return ret;
}

int main(int argc, char *argv[])
{
	const char *extra_devs[MAX_EXTRA_DEVS];
	int nr_extra = 0;
	const char *output = NULL;
	const char *cmd;
	const char *image;
	const char *path;
	struct btrfs_fs_info *fs_info;
	bool long_fmt = false;
	bool is_list;
	int opt;
	int ret;
	int i;

	if (argc < 2) {
		usage(stderr);
		return 2;
	}
	cmd = argv[1];
	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) {
		usage(stdout);
		return 0;
	}
	if (!strcmp(cmd, "list") || !strcmp(cmd, "ls")) {
		is_list = true;
	} else if (!strcmp(cmd, "get") || !strcmp(cmd, "cat")) {
		is_list = false;
	} else {
		fprintf(stderr, "%s: unknown command '%s'\n", prog, cmd);
		usage(stderr);
		return 2;
	}

	/* Parse options after the subcommand. */
	argc--;
	argv++;
	while ((opt = getopt(argc, argv, "hlo:d:")) != -1) {
		switch (opt) {
		case 'l':
			long_fmt = true;
			break;
		case 'o':
			output = optarg;
			break;
		case 'd':
			if (nr_extra >= MAX_EXTRA_DEVS) {
				fprintf(stderr, "%s: too many devices\n", prog);
				return 2;
			}
			extra_devs[nr_extra++] = optarg;
			break;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
	}
	argc -= optind;
	argv += optind;

	if (is_list ? (argc < 1 || argc > 2) : argc != 2) {
		usage(stderr);
		return 2;
	}
	image = argv[0];
	path = argc > 1 ? argv[1] : "/";

	for (i = 0; i < nr_extra; i++) {
		ret = btrfs_scan_device(extra_devs[i], NULL);
		if (ret < 0) {
			fprintf(stderr, "%s: %s: not a usable btrfs device: %s\n",
				prog, extra_devs[i], strerror(-ret));
			btrfs_exit();
			return 1;
		}
	}

	fs_info = btrfs_mount(image);
	if (IS_ERR(fs_info)) {
		fprintf(stderr, "%s: %s: failed to open btrfs filesystem: %s\n",
			prog, image, strerror((int)-PTR_ERR(fs_info)));
		btrfs_exit();
		return 1;
	}

	if (is_list)
		ret = cmd_list(fs_info, path, long_fmt);
	else
		ret = cmd_get(fs_info, path, output);

	btrfs_unmount(fs_info);
	btrfs_exit();
	return ret < 0 ? 1 : 0;
}
