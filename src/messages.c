// SPDX-License-Identifier: GPL-2.0-only
/*
 * Replacement for btrfs-fuse's messages.c:
 *  - expands glibc's "%m" (strerror(errno)), which macOS printf lacks;
 *  - sends every message to stderr, so `btrfs-cli get` can stream file
 *    data to stdout without diagnostics mixed in.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "messages.h"

static void vmessage(const char *prefix, const char *fmt, va_list args)
{
	const char *err = strerror(errno);
	char expanded[1024];
	size_t out = 0;
	const char *p;

	/* Rewrite %m to the errno text; leave %%m alone. */
	for (p = fmt; *p && out < sizeof(expanded) - 1; p++) {
		if (p[0] == '%' && p[1] == '%') {
			if (out + 2 >= sizeof(expanded))
				break;
			expanded[out++] = *p++;
			expanded[out++] = *p;
		} else if (p[0] == '%' && p[1] == 'm') {
			const char *e;

			for (e = err; *e && out < sizeof(expanded) - 2; e++) {
				/* Escape any '%' in the errno text. */
				if (*e == '%')
					expanded[out++] = '%';
				expanded[out++] = *e;
			}
			p++;
		} else {
			expanded[out++] = *p;
		}
	}
	expanded[out] = '\0';

	fputs(prefix, stderr);
	vfprintf(stderr, expanded, args);
	fputc('\n', stderr);
}

#pragma clang diagnostic ignored "-Wformat-nonliteral"

void error(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vmessage("ERROR: ", fmt, args);
	va_end(args);
}

void warning(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vmessage("WARNING: ", fmt, args);
	va_end(args);
}

void info(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vmessage("INFO: ", fmt, args);
	va_end(args);
}

void debug(const char *fmt, ...)
{
#ifdef DEBUG
	va_list args;

	va_start(args, fmt);
	vmessage("DEBUG: ", fmt, args);
	va_end(args);
#else
	(void)fmt;
#endif
}
