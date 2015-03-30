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
