/*
 * Simulate ANSI/POSIX conformance on platforms that don't have it
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <time.h>
#include <sys/time.h>

#include "compiler.h"

#ifndef HAVE_CLOCK_GETTIME

/*
 * Note that previous versions of this code made use of clock_get_time()
 * on OSX, as a way to get time of day with nanosecond resolution.  But
 * it turns out that clock_get_time() only has microsecond resolution,
 * in spite of the data format, and it's also substantially slower than
 * gettimeofday().  Thus, it makes no sense to do anything special for OSX.
 */

int clock_gettime(clockid_t clk_id UNUSED, struct timespec *ts)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0)
	return -1;
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000;
    return 0;
}
#endif /* HAVE_CLOCK_GETTIME */

/* end */
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "gpsd_config.h"
#ifndef HAVE_DAEMON
#if defined (HAVE_PATH_H)
#include <paths.h>
#else
#if !defined (_PATH_DEVNULL)
#define _PATH_DEVNULL    "/dev/null"
#endif
#endif

int daemon(int nochdir, int noclose)
/* compatible with the daemon(3) found on Linuxes and BSDs */
{
    int fd;

    switch (fork()) {
    case -1:
	return -1;
    case 0:			/* child side */
	break;
    default:			/* parent side */
	exit(EXIT_SUCCESS);
    }

    if (setsid() == -1)
	return -1;
    if ((nochdir==0) && (chdir("/") == -1))
	return -1;
    if ((noclose==0) && (fd = open(_PATH_DEVNULL, O_RDWR, 0)) != -1) {
	(void)dup2(fd, STDIN_FILENO);
	(void)dup2(fd, STDOUT_FILENO);
	(void)dup2(fd, STDERR_FILENO);
	if (fd > 2)
	    (void)close(fd);
    }
    /* coverity[leaked_handle] Intentional handle duplication */
    return 0;
}

#endif /* HAVE_DAEMON */

// end
/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <string.h>
#include <time.h>       /* for time_t */
#include "gpsd_config.h"

/*
 * These versions use memcpy and strlen() because they are often
 * heavily optimized down to assembler level. Thus, likely to be
 * faster even with the function call overhead.
 */

#ifndef HAVE_STRLCAT
/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t strlcat(char *dst, const char *src, size_t siz)
{
    size_t slen = strlen(src);
    size_t dlen = strlen(dst);
    if (siz != 0) {
	if (dlen + slen < siz)
	    memcpy(dst + dlen, src, slen + 1);
	else {
	    memcpy(dst + dlen, src, siz - dlen - 1);
	    dst[siz - 1] = '\0';
	}
    }
    return dlen + slen;
}

#ifdef __UNUSED__
/*	$OpenBSD: strlcat.c,v 1.13 2005/08/08 08:05:37 espie Exp $	*/

/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

size_t strlcat(char *dst, const char *src, size_t siz)
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;
    size_t dlen;

    /* Find the end of dst and adjust bytes left but don't go past end */
    while (n-- != 0 && *d != '\0')
	d++;
    dlen = (size_t) (d - dst);
    n = siz - dlen;

    if (n == 0)
	return (dlen + strlen(s));
    while (*s != '\0') {
	if (n != 1) {
	    *d++ = *s;
	    n--;
	}
	s++;
    }
    *d = '\0';

    return (dlen + (s - src));	/* count does not include NUL */
}
#endif /* __UNUSED__ */
#endif /* HAVE_STRLCAT */

#ifndef HAVE_STRLCPY
/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t strlcpy(char *dst, const char *src, size_t siz)
{
    size_t len = strlen(src);
    if (siz != 0) {
	if (len >= siz) {
	    memcpy(dst, src, siz - 1);
	    dst[siz - 1] = '\0';
	} else
	    memcpy(dst, src, len + 1);
    }
    return len;
}

#ifdef __UNUSED__
/*	$OpenBSD: strlcpy.c,v 1.11 2006/05/05 15:27:38 millert Exp $	*/

/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
size_t strlcpy(char *dst, const char *src, size_t siz)
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0) {
	while (--n != 0) {
	    if ((*d++ = *s++) == '\0')
		break;
	}
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
	if (siz != 0)
	    *d = '\0';		/* NUL-terminate dst */
	while (*s++ != '\0')
	    continue;
    }

    return ((size_t) (s - src - 1));	/* count does not include NUL */
}
#endif /* __UNUSED__ */
#endif /* HAVE_STRLCPY */
