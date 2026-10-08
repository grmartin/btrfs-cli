// SPDX-License-Identifier: GPL-2.0-only
/*
 * Force-included (-include) into every translation unit so the unmodified
 * btrfs-fuse sources build on macOS.
 */
#ifndef BTRFS_CLI_COMPAT_PRELUDE_H
#define BTRFS_CLI_COMPAT_PRELUDE_H

#include <sys/stat.h>
#include <sys/types.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>

/* Linux struct stat names its timespec members st_[amc]tim. */
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec

/* Linux "Structure needs cleaning", used for on-disk corruption. */
#ifndef EUCLEAN
#define EUCLEAN EFTYPE
#endif

/*
 * btrfs_read_from_disk() loops until pread() fills the buffer, and spins
 * forever when pread() returns 0 at end of file (truncated or non-btrfs
 * image). Route it through a wrapper that reports EOF as EIO instead.
 */
ssize_t btrfs_cli_pread(int fd, void *buf, size_t count, off_t offset);
#define pread btrfs_cli_pread

/* glibc <sys/cdefs.h> extension. */
#ifndef __attribute_const__
#define __attribute_const__ __attribute__((__const__))
#endif

#endif
