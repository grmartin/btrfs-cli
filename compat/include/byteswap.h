// SPDX-License-Identifier: GPL-2.0-only
/*
 * macOS stand-in for glibc's <byteswap.h>.
 *
 * Also defines the glibc __BYTE_ORDER macros: btrfs-fuse's compat.h tests
 * "__BYTE_ORDER == __BIG_ENDIAN", which is true (0 == 0) when neither is
 * defined, and would byte-swap every on-disk field on little-endian hosts.
 */
#ifndef BTRFS_CLI_COMPAT_BYTESWAP_H
#define BTRFS_CLI_COMPAT_BYTESWAP_H

#include <libkern/OSByteOrder.h>

#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN __ORDER_BIG_ENDIAN__
#endif
#ifndef __BYTE_ORDER
#define __BYTE_ORDER __BYTE_ORDER__
#endif

#endif
