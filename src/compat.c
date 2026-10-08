// SPDX-License-Identifier: GPL-2.0-only
/* Runtime half of compat/prelude.h. */
#include <errno.h>
#include <unistd.h>

#undef pread

ssize_t btrfs_cli_pread(int fd, void *buf, size_t count, off_t offset)
{
	ssize_t ret = pread(fd, buf, count, offset);

	if (ret == 0 && count > 0) {
		errno = EIO;
		return -1;
	}
	return ret;
}
